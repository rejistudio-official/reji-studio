// Faz 1 Aşama 6 — gerçek obs-websocket-js (Companion'ın kullandığı kütüphane) uyum testi.
//
// Kurulum:  npm install obs-websocket-js
// Çalıştır: node scripts/test_obs_websocket_js.js [ws://127.0.0.1:7071/ws]
//
// Bu araç, Reji'nin obs-websocket alt-protokol müzakeresini ve JSON uyumunu gerçek
// üçüncü-parti istemciyle doğrular (Aşama 6 kök bulgusunu ortaya çıkaran araç).
//
// obs-websocket-js 5.x davranışı (deneyle doğrulandı):
//   - Node varsayılanı (require('obs-websocket-js'))       → obswebsocket.MSGPACK teklif eder
//   - JSON modu       (require('obs-websocket-js/json'))   → obswebsocket.JSON teklif eder
//   - Her iki modda da sunucunun teklif edilen alt-protokolü SEÇMESİNİ bekler; seçilmezse
//     "Server sent no subprotocol" ile koparır.
//
// Reji (Aşama 6 sonrası): obswebsocket.json seçer → JSON modu ÇALIŞIR; msgpack modu
// hâlâ bağlanamaz (msgpack serileştirme Aşama 7 adayı). Bu script iki modu da dener.

const URL = process.argv[2] || 'ws://127.0.0.1:7071/ws';

async function tryMode(modulePath, label) {
  const { OBSWebSocket } = require(modulePath);
  const obs = new OBSWebSocket();
  try {
    const { obsWebSocketVersion, negotiatedRpcVersion } = await obs.connect(URL);
    console.log(`[${label}] OK — Identify başarılı: ${obsWebSocketVersion} rpc=${negotiatedRpcVersion}`);
    const sl = await obs.call('GetSceneList');
    console.log(`[${label}] GetSceneList sahne sırası (sceneIndex 0 = OBS konvansiyonunda en alt):`);
    for (const s of sl.scenes) {
      console.log(`   sceneIndex=${s.sceneIndex}  sceneName=${JSON.stringify(s.sceneName)}`);
    }
    console.log(`[${label}] currentProgramSceneName: ${JSON.stringify(sl.currentProgramSceneName)}`);
    await obs.disconnect();
    return true;
  } catch (err) {
    console.log(`[${label}] FAIL — ${err.message}`);
    return false;
  }
}

(async () => {
  console.log(`Hedef: ${URL}\n`);
  console.log('--- JSON modu (obs-websocket-js/json — browser default / opt-in) ---');
  await tryMode('obs-websocket-js/json', 'json');
  console.log('\n--- msgpack modu (obs-websocket-js — Node/Companion default) ---');
  await tryMode('obs-websocket-js', 'msgpack');
})();
