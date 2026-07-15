"""Faz 1 Aşama 6 — gerçek obs-websocket istemcisiyle doğrulama (simpleobsws).

Kurulum: pip install simpleobsws

simpleobsws msgpack-only bir istemcidir (subprotocols=['obswebsocket.msgpack'],
yalnızca binary frame okur). Aşama 7 ile Reji msgpack konuşuyor → bu script artık
Reji'ye uçtan uca bağlanıp çalışır (Aşama 6'da bağlanamıyordu — o dönemin bulgusu).
JSON-modu doğrulama için test_obs_json.py kullan.

Port: ffi.rs → ws_server::serve(vec![7070, 7071, 7072, 7073]) — 7070 öncelikli.
"""
import asyncio
import sys

import simpleobsws

# ffi.rs sırasıyla 7070→7073 dener; ilk boş porta bağlanır. 7070 başka bir
# uygulama (ör. AnyDesk) tarafından tutuluyorsa Reji 7071'e düşer. TCP'nin açık
# olması bizim WS sunucumuz olduğunu kanıtlamaz, o yüzden portu gerçek obs-ws
# Identify handshake'iyle tespit ediyoruz (talimat: "gerçek portu doğrula").
CANDIDATE_PORTS = [7070, 7071, 7072, 7073]
REAL_SCENE = "Sahne 1"      # varsayılan sahnelerden biri (main_window.cpp)
MISSING_SCENE = "___yok___"


async def detect_ws():
    """İlk Identify olan porta bağlı, identify edilmiş client'ı döndürür."""
    for port in CANDIDATE_PORTS:
        url = f"ws://127.0.0.1:{port}/ws"
        ws = simpleobsws.WebSocketClient(url=url)
        try:
            await asyncio.wait_for(ws.connect(), timeout=1.0)
            await asyncio.wait_for(ws.wait_until_identified(), timeout=1.5)
            return ws, url
        except Exception:
            try:
                await ws.disconnect()
            except Exception:
                pass
    raise SystemExit("Reji obs-websocket sunucusu bulunamadı (7070-7073)")


def dump(title, resp):
    print(f"\n--- {title} ---")
    print("ok        :", resp.ok())
    print("requestStatus:", resp.requestStatus)
    print("responseData:", resp.responseData)


def on_vendor_event(eventData):
    """Özellik#2 (Aşama A): Reji healing VendorEvent'lerini yazdırır.

    obs-websocket VendorEvent eventData'sı: {vendorName, eventType, eventData}.
    Reji'nin yaydığı eventType'lar: HealingModeChanged, HealingActionApplied,
    HealingActionPending, HealingActionInvalidated. Bunları GÖRMEK için app'te
    healing tetiklenmeli (ör. Settings'ten mod değiştir → HealingModeChanged;
    veya kural motoru bir aksiyon üretsin). Bu, I8'in fiziksel-donanım
    doğrulaması gibi CANLI app gözlemidir — Rust entegrasyon testi teli doğrular,
    bu script gerçek simpleobsws istemcisiyle uçtan uca teyit içindir.
    """
    if eventData.get("vendorName") == "reji-studio":
        print(f"[VendorEvent] {eventData.get('eventType')}: {eventData.get('eventData')}")


async def main():
    ws, url = await detect_ws()
    print(f"[connected + identified] {url}")

    # Özellik#2: healing VendorEvent'lerini dinle (Reji aboneliği yoksaydığından
    # herhangi bir eventSubscriptions ile gelir; simpleobsws op-5 event'i callback'e
    # dağıtır). Script çalışırken app'te mod değiştirince satır düşmeli.
    ws.register_event_callback(on_vendor_event, "VendorEvent")
    print("[listening] VendorEvent (healing) — app'te healing tetikleyin (ör. mod değiştir)")

    dump("GetVersion", await ws.call(simpleobsws.Request("GetVersion")))

    sl = await ws.call(simpleobsws.Request("GetSceneList"))
    dump("GetSceneList", sl)
    # Soru 1 için sceneIndex/sceneName eşlemesini açıkça yazdır
    scenes = sl.responseData.get("scenes", [])
    print("  scene order (as returned):")
    for s in scenes:
        print(f"    sceneIndex={s['sceneIndex']}  sceneName={s['sceneName']!r}  uuid={s['sceneUuid']}")
    print("  currentProgramSceneName:", sl.responseData.get("currentProgramSceneName"))

    dump("GetStreamStatus (idle)", await ws.call(simpleobsws.Request("GetStreamStatus")))

    dump("SetCurrentProgramScene (var olan)",
         await ws.call(simpleobsws.Request("SetCurrentProgramScene", {"sceneName": REAL_SCENE})))
    dump("SetCurrentProgramScene (olmayan)",
         await ws.call(simpleobsws.Request("SetCurrentProgramScene", {"sceneName": MISSING_SCENE})))

    print("\n=== Stream lifecycle ===")
    dump("StartStream", await ws.call(simpleobsws.Request("StartStream")))
    await asyncio.sleep(2)
    dump("GetStreamStatus (after start)", await ws.call(simpleobsws.Request("GetStreamStatus")))
    dump("StopStream", await ws.call(simpleobsws.Request("StopStream")))
    await asyncio.sleep(1)
    dump("GetStreamStatus (after stop)", await ws.call(simpleobsws.Request("GetStreamStatus")))

    await ws.disconnect()
    print("\n[disconnected] done.")


if __name__ == "__main__":
    if len(sys.argv) > 1:
        REAL_SCENE = sys.argv[1]
    asyncio.run(main())
