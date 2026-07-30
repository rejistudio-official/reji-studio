"""Reji vs OBS karşılaştırmalı benchmark aracı.

Kullanım:
  python scripts/benchmark_compare.py [SEÇENEKLER]

Her iki uygulama da önceden başlatılmış ve yayın yapıyor olmalı.
Script yalnızca çalışan WS sunucularına bağlanır — başlatma/durdurma yapmaz.

Kurulum: pip install websockets
"""
import argparse
import asyncio
import base64
import csv
import datetime
import hashlib
import json
import os
import sys
from typing import Optional

from websockets.asyncio.client import connect as ws_connect

REJI_PORTS = [7070, 7071, 7072, 7073]
OBS_DEFAULT_PORT = 4455
CSV_FIELDS = ["source", "timestamp", "fps", "bitrate_kbps", "dropped_frames", "output_active"]

# ──────────────────────────────────────────────────────────────────────────────
# Yardımcılar
# ──────────────────────────────────────────────────────────────────────────────


def compute_auth(password: str, salt: str, challenge: str) -> str:
    """obs-websocket v5 auth string'i üret.

    Spec: secret = base64(sha256(password + salt))
          auth   = base64(sha256(secret + challenge))
    """
    def b64_sha256(s: str) -> str:
        return base64.b64encode(hashlib.sha256(s.encode()).digest()).decode()

    secret = b64_sha256(password + salt)
    return b64_sha256(secret + challenge)


def utcnow() -> str:
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3] + "Z"


async def _recv_skip_to_op(ws, op: int, timeout: float = 3.0) -> dict:
    """Verilen op koduna sahip mesajı alana kadar diğerlerini atla."""
    while True:
        raw = await asyncio.wait_for(ws.recv(), timeout=timeout)
        # websockets 16: recv() str | bytes döndürebilir
        if isinstance(raw, bytes):
            raw = raw.decode()
        msg = json.loads(raw)
        if msg.get("op") == op:
            return msg


async def connect_and_identify(host: str, port: int, password: str):
    """Bağlan → Hello → Identify (gerekirse auth) → Identified."""
    uri = f"ws://{host}:{port}/ws"
    ws = await ws_connect(
        uri,
        open_timeout=3.0,
        subprotocols=["obswebsocket.json"],
    )

    hello = await _recv_skip_to_op(ws, op=0, timeout=3.0)

    identify_d: dict = {"rpcVersion": 1}
    auth_info = hello["d"].get("authentication")
    if auth_info:
        if not password:
            await ws.close()
            raise RuntimeError(
                f"{uri} sunucusu parola istiyor; --reji-pass / --obs-pass ile ver."
            )
        identify_d["authentication"] = compute_auth(
            password, auth_info["salt"], auth_info["challenge"]
        )

    await ws.send(json.dumps({"op": 1, "d": identify_d}))

    # Arada legacy metric event gelebilir → op=2 gelene kadar atla
    await _recv_skip_to_op(ws, op=2, timeout=5.0)
    return ws


async def _ws_request(ws, req_type: str, req_id: str, timeout: float = 5.0) -> dict:
    """op 6 isteği gönder, eşleşen op 7 yanıtını bekle (event'leri atla)."""
    await ws.send(json.dumps({
        "op": 6,
        "d": {"requestType": req_type, "requestId": req_id},
    }))
    while True:
        raw = await asyncio.wait_for(ws.recv(), timeout=timeout)
        if isinstance(raw, bytes):
            raw = raw.decode()
        msg = json.loads(raw)
        if msg.get("op") == 7 and msg["d"].get("requestId") == req_id:
            return msg["d"].get("responseData") or {}


# ──────────────────────────────────────────────────────────────────────────────
# Reji toplayıcı — event-driven
# ──────────────────────────────────────────────────────────────────────────────

async def _detect_reji_port(host: str) -> Optional[int]:
    """7070–7073 aralığında ilk yanıt veren Reji portunu bul."""
    for port in REJI_PORTS:
        ws = None
        try:
            ws = await ws_connect(f"ws://{host}:{port}/ws", open_timeout=1.0)
            raw = await asyncio.wait_for(ws.recv(), timeout=1.5)
            if json.loads(raw).get("op") == 0:
                return port
        except Exception:
            pass
        finally:
            if ws:
                try:
                    await ws.close()
                except Exception:
                    pass
    return None


async def reji_collect(
    host: str, port: int, password: str, duration: float, rows: list
) -> None:
    """Reji'ye bağlan; legacy metric event'lerden örnekler topla.

    Reji, JSON bağlantılarda ~60Hz (video karesi başına bir örnek, ~16.7ms)
    şu zarfı yayar (op içermez):
        {"fps": 59.8, "kbps": 3500, "drop": 2, "cpu": 45, "gpu": 78, "mem": 60}
    "fps" anlık 1/Δt'dir (run_frame döngü cadence'i; pencereli ortalama değil,
    60'ı aşabilir ve 240'a clamp'lenir). "drop" per-sample delta'dır (kümülatif
    değil). OBS 1Hz poll edildiğinden adil karşılaştırma için örnekler sonradan
    1sn pencerelere toplulaştırılır (bkz. aggregate_to_1hz).
    """
    actual_port = port or await _detect_reji_port(host)
    if not actual_port:
        print(
            "[reji] BAĞLANTI HATASI: 7070-7073 aralığında Reji bulunamadı.",
            file=sys.stderr,
        )
        return

    try:
        ws = await connect_and_identify(host, actual_port, password)
    except Exception as exc:
        print(f"[reji] BAĞLANTI HATASI: {exc}", file=sys.stderr)
        return

    print(f"[reji] Bağlandı ws://{host}:{actual_port}/ws — event bekleniyor...")
    deadline = asyncio.get_event_loop().time() + duration
    count = 0

    try:
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                break
            try:
                raw = await asyncio.wait_for(ws.recv(), timeout=min(remaining, 2.0))
            except asyncio.TimeoutError:
                continue
            except Exception:
                break

            try:
                if isinstance(raw, bytes):
                    raw = raw.decode()
                msg = json.loads(raw)
            except (json.JSONDecodeError, UnicodeDecodeError):
                continue

            # Legacy metric event: "op" yok, "fps" var
            if "fps" in msg and "op" not in msg:
                rows.append({
                    "source": "reji",
                    "timestamp": utcnow(),
                    "fps": msg["fps"],
                    "bitrate_kbps": msg.get("kbps", ""),
                    "dropped_frames": msg.get("drop", ""),
                    "output_active": "",  # event yapısında yok
                })
                count += 1
    finally:
        try:
            await ws.close()
        except Exception:
            pass

    print(f"[reji] Tamamlandı — {count} örnek.")


# ──────────────────────────────────────────────────────────────────────────────
# OBS toplayıcı — interval-driven poll
# ──────────────────────────────────────────────────────────────────────────────

async def obs_collect(
    host: str, port: int, password: str,
    duration: float, interval: float,
    rows: list,
) -> None:
    """OBS'e bağlan; GetStats + GetStreamStatus ile periyodik örnekler topla.

    fps          ← GetStats.activeFps
    dropped      ← Δ(GetStreamStatus.outputSkippedFrames)   [kümülatif → delta]
    bitrate_kbps ← Δ(GetStreamStatus.outputBytes) / interval / 125  [yaklaşık]
    """
    try:
        ws = await connect_and_identify(host, port, password)
    except Exception as exc:
        print(f"[obs] BAĞLANTI HATASI: {exc}", file=sys.stderr)
        return

    print(f"[obs] Bağlandı ws://{host}:{port}/ws — {interval}s aralıkla polling...")
    deadline = asyncio.get_event_loop().time() + duration
    prev_skipped: Optional[int] = None
    prev_bytes: Optional[int] = None
    req_n = 0
    count = 0

    try:
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                break
            tick = asyncio.get_event_loop().time()
            req_n += 1

            try:
                stats = await _ws_request(ws, "GetStats", f"gs{req_n}")
                status = await _ws_request(ws, "GetStreamStatus", f"ss{req_n}")
            except Exception as exc:
                print(f"[obs] istek hatası: {exc}", file=sys.stderr)
                break

            ts = utcnow()
            fps = stats.get("activeFps", "")
            curr_skipped: int = status.get("outputSkippedFrames") or 0
            curr_bytes: int = status.get("outputBytes") or 0
            output_active = status.get("outputActive", "")

            dropped: object = ""
            bitrate_kbps: object = ""
            if prev_skipped is not None:
                dropped = max(0, curr_skipped - prev_skipped)
            if prev_bytes is not None:
                byte_delta = max(0, curr_bytes - prev_bytes)
                # bytes / s → kbit/s: (delta / interval_s) * 8 / 1000 = delta / interval / 125
                bitrate_kbps = round(byte_delta / interval / 125)

            prev_skipped = curr_skipped
            prev_bytes = curr_bytes

            rows.append({
                "source": "obs",
                "timestamp": ts,
                "fps": round(fps, 2) if isinstance(fps, float) else fps,
                "bitrate_kbps": bitrate_kbps,
                "dropped_frames": dropped,
                "output_active": str(output_active).lower() if output_active != "" else "",
            })
            count += 1

            elapsed = asyncio.get_event_loop().time() - tick
            sleep_for = min(max(0.0, interval - elapsed), remaining)
            if sleep_for > 0:
                await asyncio.sleep(sleep_for)
    finally:
        try:
            await ws.close()
        except Exception:
            pass

    print(f"[obs] Tamamlandı — {count} örnek.")


# ──────────────────────────────────────────────────────────────────────────────
# Özet
# ──────────────────────────────────────────────────────────────────────────────

def _numeric(seq):
    return [v for v in seq if isinstance(v, (int, float)) and v != ""]


def _coerce_num(v):
    """CSV'den okunan string'i ya da canlı koşumdaki sayıyı float'a çevir.

    Boş hücre / çevrilemeyen değer → None (agregasyonda elenmesi için).
    """
    if isinstance(v, (int, float)):
        return v
    if v is None or v == "":
        return None
    try:
        return float(v)
    except (TypeError, ValueError):
        return None


def _floor_to_second(ts: str) -> str:
    """'2026-07-30T21:42:44.930Z' → '2026-07-30T21:42:44Z' (saniyeye yuvarla)."""
    base = ts.split(".", 1)[0]
    return base if base.endswith("Z") else base + "Z"


def _frac_second(ts: str) -> float:
    """Timestamp'in saniye-kesirini [0,1) döndür ('...:23.081Z' → 0.081)."""
    if "." in ts:
        return float("0." + ts.split(".", 1)[1].rstrip("Z"))
    return 0.0


# Kısmi uç penceresi eşiği: ilk/son 1sn kovası bu orandan az wall-clock
# kapsıyorsa (örnekleme saniyenin ortasında başlayıp/bittiyse) özet dışı bırakılır.
EDGE_MIN_COVERAGE = 0.9


def aggregate_to_1hz(rows: list) -> list:
    """Ham satırları kaynak + 1sn pencerelerine toplulaştır (1Hz).

    Reji ~60Hz örnekliyor (video karesi başına bir örnek); OBS 1Hz poll ediliyor.
    Aynı zaman çözünürlüğünde adil kıyas için pencere kuralları:
        fps   → REJI: pencere içi kare SAYISI (= kare/1sn). Anlık 1/Δt'nin
                       aritmetik ortalaması yerine gerçek oran — bu, harmonik
                       ortalamaya eşittir ve OBS activeFps ile doğrudan denktir.
                 OBS: activeFps zaten düzgün oran → pencere ORTALAMASI.
        drop  → pencere içi TOPLAM     (per-sample delta → saniyedeki kare kaybı)
        kbps  → pencere ORTALAMASI     (anlık bitrate → 1sn ortalama oran)
        output_active → penceredeki son boş-olmayan değer
    min-FPS özette bu saniyelik değerlerin min'i olarak çıkar. Kısmi uç
    pencereler (tam saniye içermeyen ilk/son kova) elenir — eksik örnekli kova
    aksi halde sahte (düşük/yüksek) fps üretir.
    """
    from collections import OrderedDict

    by_source: "OrderedDict[str, list]" = OrderedDict()
    for r in sorted(rows, key=lambda r: (r["source"], r["timestamp"])):
        by_source.setdefault(r["source"], []).append(r)

    out = []
    for source, srows in by_source.items():
        buckets: "OrderedDict[str, list]" = OrderedDict()
        for r in srows:
            buckets.setdefault(_floor_to_second(r["timestamp"]), []).append(r)
        seconds = list(buckets.keys())  # srows ts-sıralı → kovalar zaman sıralı
        n_sec = len(seconds)

        for idx, second in enumerate(seconds):
            group = buckets[second]

            # Kısmi uç pencere elemesi (yalnız ≥3 kova varsa; hepsini silme).
            if n_sec >= 3:
                if idx == 0 and (1.0 - _frac_second(group[0]["timestamp"])) < EDGE_MIN_COVERAGE:
                    continue
                if idx == n_sec - 1 and _frac_second(group[-1]["timestamp"]) < EDGE_MIN_COVERAGE:
                    continue

            kbps_v = [n for n in (_coerce_num(g["bitrate_kbps"]) for g in group) if n is not None]
            drop_v = [n for n in (_coerce_num(g["dropped_frames"]) for g in group) if n is not None]
            fps_v = [n for n in (_coerce_num(g["fps"]) for g in group) if n is not None]

            if source == "reji":
                # Kare başına bir örnek → o saniyedeki gerçek fps = kare sayısı.
                fps_out: object = len(group) if group else ""
            else:
                # OBS: activeFps zaten oran, 1Hz → ortalama (tek değer de olsa).
                fps_out = round(sum(fps_v) / len(fps_v), 2) if fps_v else ""

            oa = ""
            for g in group:
                if g.get("output_active"):
                    oa = g["output_active"]

            out.append({
                "source": source,
                "timestamp": second,
                "fps": fps_out,
                "bitrate_kbps": round(sum(kbps_v) / len(kbps_v)) if kbps_v else "",
                "dropped_frames": int(sum(drop_v)) if drop_v else "",
                "output_active": oa,
            })

    out.sort(key=lambda r: r["timestamp"])
    return out


def _agg_path(raw_path: str) -> str:
    """'benchmark_X.csv' → 'benchmark_X_1hz.csv'."""
    root, ext = os.path.splitext(raw_path)
    return f"{root}_1hz{ext or '.csv'}"


def _write_csv(path: str, rows: list) -> None:
    with open(path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=CSV_FIELDS)
        writer.writeheader()
        for r in rows:
            writer.writerow(r)


def print_summary(rows: list) -> None:
    print(f"\n{'='*60}")
    print(f"{'BENCHMARK ÖZET (1Hz toplulaştırılmış)':^60}")
    print(f"{'='*60}")
    header = f"{'':6s}  {'FPS ort/min':>14s}  {'Bitrate ort kbps':>18s}  {'Dropped toplam':>14s}"
    print(header)
    print("-" * 60)
    for source in ("reji", "obs"):
        src = [r for r in rows if r["source"] == source]
        if not src:
            continue
        fps_v = _numeric([r["fps"] for r in src])
        drop_v = _numeric([r["dropped_frames"] for r in src])
        kbps_v = _numeric([r["bitrate_kbps"] for r in src])

        fps_str = (
            f"{sum(fps_v)/len(fps_v):.1f}/{min(fps_v):.1f}"
            if fps_v else "N/A"
        )
        kbps_str = f"{round(sum(kbps_v)/len(kbps_v))}" if kbps_v else "N/A"
        drop_str = str(sum(drop_v)) if drop_v else "N/A"

        print(f"{source.upper():6s}  {fps_str:>14s}  {kbps_str:>18s}  {drop_str:>14s}")


# ──────────────────────────────────────────────────────────────────────────────
# Giriş noktası
# ──────────────────────────────────────────────────────────────────────────────

async def run(args) -> None:
    ts_str = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%d_%H%M%S")
    csv_path = args.output or f"benchmark_{ts_str}.csv"

    rows: list = []  # asyncio tek-thread → GIL'siz paylaşım güvenli

    tasks = []
    if args.target in ("reji", "both"):
        tasks.append(reji_collect(
            "127.0.0.1", args.reji_port, args.reji_pass,
            args.duration, rows,
        ))
    if args.target in ("obs", "both"):
        tasks.append(obs_collect(
            "127.0.0.1", args.obs_port, args.obs_pass,
            args.duration, args.interval, rows,
        ))

    print(
        f"Başlıyor: hedef={args.target}, süre={args.duration}s, "
        f"interval={args.interval}s"
    )
    await asyncio.gather(*tasks, return_exceptions=True)

    # Timestamp'e göre sırala — iki kaynağın satırları karışık gelebilir
    rows.sort(key=lambda r: r["timestamp"])

    # Ham veri (~20Hz Reji / 1Hz OBS) tam çözünürlükte korunur.
    _write_csv(csv_path, rows)

    # Adil karşılaştırma için 1sn pencerelere toplula; özet bunu kullanır.
    agg_rows = aggregate_to_1hz(rows)
    agg_csv_path = _agg_path(csv_path)
    _write_csv(agg_csv_path, agg_rows)

    print_summary(agg_rows)
    print(f"\nHam CSV kaydedildi:  {csv_path}")
    print(f"1Hz CSV kaydedildi:  {agg_csv_path}")


def reprocess(csv_in: str, csv_out: str = "") -> None:
    """Var olan ham CSV'yi 1Hz'e toplula ve özetle — hiçbir ağ bağlantısı kurmaz."""
    with open(csv_in, newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))
    agg_rows = aggregate_to_1hz(rows)
    out_path = csv_out or _agg_path(csv_in)
    _write_csv(out_path, agg_rows)
    print(
        f"Yeniden işlendi: {csv_in} ({len(rows)} ham satır) "
        f"-> {out_path} ({len(agg_rows)} 1Hz satır)"
    )
    print_summary(agg_rows)
    print(f"\n1Hz CSV kaydedildi: {out_path}")


def main() -> None:
    p = argparse.ArgumentParser(
        description="Reji vs OBS karşılaştırmalı kare-düşürme/gecikme benchmark aracı",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    p.add_argument(
        "--reji-port", type=int, default=0, metavar="PORT",
        help="Reji WS portu (0 = 7070-7073 otomatik keşif)",
    )
    p.add_argument(
        "--obs-port", type=int, default=OBS_DEFAULT_PORT, metavar="PORT",
        help="OBS WS portu",
    )
    p.add_argument("--reji-pass", default="", metavar="PAROLA", help="Reji WS şifresi")
    p.add_argument("--obs-pass", default="", metavar="PAROLA", help="OBS WS şifresi")
    p.add_argument(
        "--duration", type=float, default=60.0, metavar="SN",
        help="Örnekleme süresi (saniye)",
    )
    p.add_argument(
        "--interval", type=float, default=1.0, metavar="SN",
        help="OBS poll aralığı (saniye)",
    )
    p.add_argument(
        "--output", default="", metavar="DOSYA",
        help="CSV dosya adı (boş = benchmark_YYYYMMDD_HHMMSS.csv)",
    )
    p.add_argument(
        "--target", choices=["reji", "obs", "both"], default="both",
        help="Ölçülecek uygulama",
    )
    p.add_argument(
        "--reprocess", default="", metavar="CSV",
        help="Var olan ham CSV'yi 1Hz'e toplula ve özetle (ölçüm/bağlantı yapmaz)",
    )
    args = p.parse_args()
    if args.reprocess:
        reprocess(args.reprocess, args.output)
        return
    asyncio.run(run(args))


if __name__ == "__main__":
    main()
