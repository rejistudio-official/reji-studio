"""Faz 1 Aşama 6 — spec-uyumlu JSON obs-websocket v5 istemcisiyle doğrulama.

simpleobsws msgpack-only olduğu ve Reji sunucusu yalnızca JSON konuştuğu için
(bkz. SESSION_NOTES Aşama 6 msgpack bulgusu), gerçek obs-ws v5 handshake'ini
JSON serileştirmeyle elle yapan bir istemci. control.html ve birçok üçüncü-parti
istemci bu JSON modunu kullanır.

Kapsam:
  soru 1 — GetSceneList sceneIndex/sceneName sırası (UI ile karşılaştır)
  soru 2 — pseudo-UUID gerçek çağrılarda sorun çıkarıyor mu
  soru 3 — Identify'lı + Identify'sız (legacy) iki bağlantı paralel çalışıyor mu
  soru 4 — StartStream/StopStream gözlemlenebilir bir şey yapıyor mu
"""
import asyncio
import json

import websockets

CANDIDATE_PORTS = [7070, 7071, 7072, 7073]
REAL_SCENE = "Sahne 1"
MISSING_SCENE = "___yok___"
TURKISH_SCENE = "ŞİĞ Çöğüş sahnesi"  # encoding testi (olmayan isim → 600 beklenir)


async def obs_connect(port):
    """Bir porta bağlanıp Hello alır; obs-ws sunucusuysa (ws, hello) döndürür."""
    uri = f"ws://127.0.0.1:{port}/ws"
    ws = await websockets.connect(uri, open_timeout=1.0)
    hello = json.loads(await asyncio.wait_for(ws.recv(), timeout=1.5))
    if hello.get("op") != 0:
        await ws.close()
        raise ValueError("Hello değil")
    return ws, hello


async def find_server():
    for port in CANDIDATE_PORTS:
        try:
            ws, hello = await obs_connect(port)
            return ws, hello, port
        except Exception:
            continue
    raise SystemExit("Reji obs-websocket (JSON) sunucusu bulunamadı 7070-7073")


async def identify(ws):
    await ws.send(json.dumps({"op": 1, "d": {"rpcVersion": 1}}))
    ident = json.loads(await asyncio.wait_for(ws.recv(), timeout=2))
    assert ident.get("op") == 2, f"Identified beklendi, gelen: {ident}"
    return ident


_req_id = 0


async def request(ws, request_type, request_data=None):
    global _req_id
    _req_id += 1
    rid = str(_req_id)
    msg = {"op": 6, "d": {"requestType": request_type, "requestId": rid}}
    if request_data is not None:
        msg["d"]["requestData"] = request_data
    await ws.send(json.dumps(msg))
    # RequestResponse (op 7) gelene kadar oku (araya Event op 5 girebilir)
    while True:
        resp = json.loads(await asyncio.wait_for(ws.recv(), timeout=5))
        if resp.get("op") == 7 and resp["d"].get("requestId") == rid:
            return resp["d"]


def show(title, d):
    print(f"\n--- {title} ---")
    print("  requestStatus:", d.get("requestStatus"))
    print("  responseData :", json.dumps(d.get("responseData"), ensure_ascii=False))


async def main():
    ws, hello, port = await find_server()
    print(f"[connected] ws://127.0.0.1:{port}/ws")
    print("  Hello:", json.dumps(hello["d"], ensure_ascii=False))
    ident = await identify(ws)
    print("  Identified:", json.dumps(ident["d"], ensure_ascii=False))

    show("GetVersion", await request(ws, "GetVersion"))

    d = await request(ws, "GetSceneList")
    show("GetSceneList", d)
    scenes = d["responseData"]["scenes"]
    print("  >>> SORU 1 — sahne sırası (yanıttaki haliyle):")
    for s in scenes:
        print(f"      sceneIndex={s['sceneIndex']}  sceneName={s['sceneName']!r}  uuid={s['sceneUuid']}")
    print("      currentProgramSceneName:", d["responseData"]["currentProgramSceneName"])
    print("  >>> SORU 2 — sceneUuid alanları yukarıda; hiçbir çağrı UUID ile seçim yapmıyor.")

    show("GetStreamStatus (idle)", await request(ws, "GetStreamStatus"))

    show("SetCurrentProgramScene (var olan: %r)" % REAL_SCENE,
         await request(ws, "SetCurrentProgramScene", {"sceneName": REAL_SCENE}))
    show("SetCurrentProgramScene (olmayan: %r)" % MISSING_SCENE,
         await request(ws, "SetCurrentProgramScene", {"sceneName": MISSING_SCENE}))
    show("SetCurrentProgramScene (Türkçe olmayan: %r) — encoding" % TURKISH_SCENE,
         await request(ws, "SetCurrentProgramScene", {"sceneName": TURKISH_SCENE}))

    print("\n=== SORU 4 — Stream lifecycle ===")
    show("StartStream", await request(ws, "StartStream"))
    await asyncio.sleep(2)
    show("GetStreamStatus (after start)", await request(ws, "GetStreamStatus"))
    show("StopStream", await request(ws, "StopStream"))
    await asyncio.sleep(1)
    show("GetStreamStatus (after stop)", await request(ws, "GetStreamStatus"))

    await ws.close()
    print("\n[done]")


if __name__ == "__main__":
    asyncio.run(main())
