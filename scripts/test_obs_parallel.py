"""Faz 1 Aşama 6 — SORU 3: paralel bağlantı testi.

Aşama 1'in temel iddiası: Identify gönderen (obs) ve hiç Identify göndermeyen
(legacy, control.html gibi) iki bağlantı, birbirini etkilemeden AYNI ANDA çalışır;
5s soft-timeout dolsa bile legacy bağlantı KAPATILMAZ ve event akışı sürer.

A = identified (Identify gönderir, request yapar)
B = legacy    (hiç Identify göndermez — control.html davranışı)
"""
import asyncio
import json

import websockets

PORTS = [7070, 7071, 7072, 7073]
SOFT_TIMEOUT = 5  # ws_server.rs IDENTIFY_TIMEOUT


async def connect_obs(port):
    ws = await websockets.connect(f"ws://127.0.0.1:{port}/ws", open_timeout=1.0)
    hello = json.loads(await asyncio.wait_for(ws.recv(), timeout=1.5))
    if hello.get("op") != 0:
        await ws.close()
        raise ValueError("Hello değil")
    return ws


async def find_port():
    for p in PORTS:
        try:
            ws = await connect_obs(p)
            await ws.close()
            return p
        except Exception:
            continue
    raise SystemExit("Reji WS bulunamadı")


async def collect(ws, seconds, bucket):
    """seconds boyunca gelen mesajları bucket'a topla."""
    end = asyncio.get_event_loop().time() + seconds
    while True:
        remaining = end - asyncio.get_event_loop().time()
        if remaining <= 0:
            break
        try:
            msg = await asyncio.wait_for(ws.recv(), timeout=remaining)
            bucket.append(json.loads(msg))
        except asyncio.TimeoutError:
            break
        except websockets.ConnectionClosed:
            bucket.append({"__closed__": True})
            break


async def request(ws, request_type):
    await ws.send(json.dumps({"op": 6, "d": {"requestType": request_type, "requestId": "post"}}))
    while True:
        r = json.loads(await asyncio.wait_for(ws.recv(), timeout=5))
        if r.get("op") == 7 and r["d"].get("requestId") == "post":
            return r["d"]


async def main():
    port = await find_port()
    print(f"[port] {port}")

    a = await connect_obs(port)  # identified
    b = await connect_obs(port)  # legacy

    # A identify — araya metrik event ({"fps":..}) girebilir, op==2 gelene kadar oku
    await a.send(json.dumps({"op": 1, "d": {"rpcVersion": 1}}))
    ident = None
    for _ in range(500):
        m = json.loads(await asyncio.wait_for(a.recv(), timeout=3))
        if m.get("op") == 2:
            ident = m
            break
    print("A identified:", ident is not None, ident["d"] if ident else None)

    # B: HİÇBİR ŞEY göndermez (legacy). İki bağlantıyı soft-timeout'u aşacak
    # kadar (7s) paralel dinle.
    print(f"\n[collecting {SOFT_TIMEOUT + 2}s — soft-timeout {SOFT_TIMEOUT}s aşılıyor]")
    a_msgs, b_msgs = [], []
    await asyncio.gather(
        collect(a, SOFT_TIMEOUT + 2, a_msgs),
        collect(b, SOFT_TIMEOUT + 2, b_msgs),
    )

    a_closed = any("__closed__" in m for m in a_msgs)
    b_closed = any("__closed__" in m for m in b_msgs)
    # Legacy metrik event'leri {"fps":..,"kbps":..} "op" içermez; obs event'leri op==5.
    # Kapanma sinyali dışındaki tüm mesajlar "alınan event" sayılır.
    a_events = [m for m in a_msgs if "__closed__" not in m]
    b_events = [m for m in b_msgs if "__closed__" not in m]

    print(f"\nA (identified): {len(a_events)} event, kapandı={a_closed}")
    print(f"B (legacy)    : {len(b_events)} event, kapandı={b_closed}")
    if b_events:
        print("  B örnek event:", json.dumps(b_events[0], ensure_ascii=False)[:160])

    # Soft-timeout SONRASI: A hâlâ request yapabiliyor mu? B hâlâ bağlı mı?
    print("\n[soft-timeout sonrası] A → GetVersion:")
    d = await request(a, "GetVersion")
    print("  A request çalışıyor:", d.get("requestStatus"))

    # B soft-timeout sonrası ilk kez Identify gönderirse identified olabiliyor mu?
    await b.send(json.dumps({"op": 1, "d": {"rpcVersion": 1}}))
    # B'ye Identified gelene kadar (araya event girebilir) oku
    b_ident = None
    for _ in range(500):
        m = json.loads(await asyncio.wait_for(b.recv(), timeout=3))
        if m.get("op") == 2:
            b_ident = m
            break
    print("  B geç Identify → Identified:", b_ident is not None, b_ident["d"] if b_ident else None)

    await a.close()
    await b.close()

    print("\n=== SORU 3 SONUÇ ===")
    ok = (not a_closed and not b_closed and len(a_events) > 0 and len(b_events) > 0
          and b_ident is not None)
    print("İki bağlantı paralel, legacy kapatılmadı, ikisi de event aldı:",
          "EVET" if ok else "HAYIR — incele")


if __name__ == "__main__":
    asyncio.run(main())
