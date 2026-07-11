# CONTEXT.md — Reji Studio Proje Bağlamı

**Son güncelleme:** 11 Temmuz 2026
**Hazırlayan:** Bu sohbetin (Claude, sohbet asistanı) kendisi — Claude Code'un gerçekleştirdiği işlerin, GitHub'dan doğrudan doğrulanmış özeti.

> ⚠️ Bu dosya çok kapsamlı hazırlandı (tarihçe + güncel durum + planlar bir arada). `session-handoff` skill'inin normal disiplini CONTEXT.md'yi sadece "anlık durum" ile sınırlı tutar, tarihçeyi `SESSION_NOTES.md`/`memory.md`'ye bırakır. Bu dosya bir defalık, kapsamlı bir "yeniden başlangıç noktası" olarak hazırlandı — ileride normal disipline göre budanabilir.
>
> **Konum notu:** Bu dosya artık repo **kökünde** yaşıyor (`session-handoff` skill tanımı gereği). Eski bir kopya `docs/CONTEXT.md`'de kaldı (2 Temmuz tarihli, güncelliğini yitirmiş) — temizlenebilir.

---

## 0. Bekleyen Değişiklikler (commit'lenmemiş — 11 Temmuz durumu)

`git status --porcelain` çıktısı (bu özet yazılırken):

- `M tests/baseline_metrics.txt` — **kasıtlı olarak commit edilmiyor.** Karakterizasyon test artefaktı; `.gitignore`'da force-track edilmiş görünse de hafıza/çalışma kuralı gereği değişiklikleri commit'lenmez.
- `?? docs/FAZ1_ASAMA1_TALIMAT.md` — takip edilmeyen (untracked) Faz 1 talimat dosyası.
- `?? docs/FAZ1_CLAUDE_CODE_TALIMAT.md` — takip edilmeyen Faz 1 talimat dosyası.
- `?? docs/FAZ1_OBS_WEBSOCKET_DESIGN.md` — takip edilmeyen Faz 1 tasarım dosyası.

Bu üç dosya henüz `docs/talimatlar/` arşivine taşınmamış/commit'lenmemiş. Karar bekliyor: arşivle veya sil. Aksi belirtilmedikçe koda dokunan bir değişiklik değiller.

`master` ↔ `origin/master` senkron (ahead/behind yok).

---

## 1. Proje Nedir (kısa hatırlatma)

**Reji Studio** — C++/Rust/Zig hibrit mimarili, açık kaynak, sıfır-kopya çift-GPU canlı yayın yazılımı. Repo: `github.com/rejistudio-official/reji-studio`. Detaylı mimari için `CLAUDE.md`'ye bakın (bu dosyada tekrar edilmiyor).

**Referans donanım:** AMD Radeon 780M (iGPU) + NVIDIA RTX 4070 Laptop (dGPU).

---

## 2. Bugüne Kadar Tamamlanan Büyük İşler (Tarihçe)

### Faz 0 — Pipeline Modülerleştirme
✅ Tamamlandı (bu sohbetten önce). `Pipeline::Impl` God Object 9 alt sisteme bölündü (FramePacer, MetricsSubsystem, AudioSubsystem, OutputSubsystem, CommandRouter, EncodeSubsystem, GpuInteropSubsystem, CaptureSubsystem, RecoveryCoordinator).

### Faz 1 — obs-websocket Protokol Uyumluluğu
✅ Tamamen tamamlandı, Aşama 1-7:
- **Aşama 1:** Hello/Identify/Identified handshake (toleranslı — Identify zorunlu değil, `control.html`'i kırmıyor)
- **Aşama 2:** GetVersion/StartStream/StopStream/GetStreamStatus (temel)
- **Aşama 3:** GetStreamStatus tam alan seti + gerçek metrikler
- **Aşama 4:** Scene names FFI + gerçek `rj_user_event_scene_switch` + SetScene komutu
- **Aşama 5:** GetSceneList/SetCurrentProgramScene
- **Aşama 6:** Gerçek istemci testiyle 2 gerçek üretim hatası bulundu ve düzeltildi — subprotocol negotiation eksikliği (obs-websocket-js JSON istemcileri bağlanamıyordu), sahne sırası tersliği (OBS konvansiyonuna uyacak şekilde düzeltildi)
- **Aşama 7:** msgpack serileştirme desteği (Node/Companion varsayılan modu)

Açık kalan tek nokta: Fiziksel Stream Deck/gerçek Bitfocus Companion kurulumuyla test edilmedi — sadece kütüphane seviyesinde (obs-websocket-js, simpleobsws) doğrulandı. `ROADMAP.md`'de bu açıkça niteliklendirilmiş durumda.

### Faz 2 — RTMP Çıkışı
✅ Kod tarafı tamamlandı, Aşama 1-2.2:
- **Aşama 1:** ITransport soyutlaması gerçek hale getirildi (SrtTransport), OutputSubsystem artık doğrudan SrtOutput değil ITransport kullanıyor
- **Aşama 2.1:** Keşif — OBS'in librtmp çekirdeği (LGPL 2.1, sadece bu kısım — GPL'li `rtmp-stream.c` ALINMADI) vendored, Zig `@cImport` ile gerçek çalıştırma kanıtlandı
- **Aşama 2.2:** RtmpTransport (Zig çekirdek + C++ sarmalayıcı), yerel gerçek ingest testi başarılı (ffmpeg RTMP sunucusu, 341 kare hatasız decode)

Açık kalan: Twitch/YouTube gerçek platform ingest testi — stream key kullanıcıda, henüz yapılmadı. Yerel test güçlü bir kanıt ama platform testi tek kesin kanıt (RTMPS gerekip gerekmediği de bu testte netleşecek).

### 7 Proje Skill'i (`.claude/skills/`)
✅ Kuruldu ve sürekli güncellendi: `build-troubleshoot`, `ffi-safety-review`, `obs-ws-protocol`, `repo-hygiene`, `session-handoff`, `subsystem-extraction`, `vulkan-interop-debug`. Sonuncusu (`vulkan-interop-debug`) bu sohbette tam revizyondan geçti (aşağıya bakın, bölüm 4).

### Community Health Dosyaları
✅ README.md, CONTRIBUTING.md, LICENSE (Apache 2.0), CODEOWNERS, issue/PR template'leri eklendi.

### Graphify (kod-grafiği aracı) Değerlendirmesi
✅ Denendi, reddedildi. Yapısal sorularda isabetli ama davranışsal/negatif sorularda ("X yapılıyor mu") güvenilmez bulundu, grep/find-references'ın üstüne katma değeri kanıtlanmadı. Skill olarak eklenmedi.

### V8 Bug Planı (`docs/FABLE5_BUG_PLAN_V8.md`)
Fable 5 + Opus 4.8'in bağımsız kod taramalarından türetilen, 33 maddelik (I1-I33) öncelik sıralı düzeltme planı. Sprint 1'in neredeyse tamamı, Sprint 2'nin bir kısmı kapandı:

| Madde | Konu | Durum |
|---|---|---|
| I1 | RuleEngine hiç çağrılmıyordu (self-healing kural katmanı dead code) | ✅ Düzeltildi |
| I2 | AMD path cross-API sync yok (orijinal tanım) | ✅ Kapandı — yanlış konumlanmıştı, bkz. bölüm 4 |
| I3 | Keyed-mutex protokolü tutarsız (orijinal tanım) | ✅ Kapandı — konum `capture_dxgi.cpp`'ye taşındı, bkz. bölüm 4 |
| I4 | CPU fallback row-pitch mismatch | ✅ Düzeltildi |
| I5 | `execute_copy()` submit-öncesi layout state yazımı | ✅ Düzeltildi |
| I6 | `is_copy_ready()` shutdown ile yarışı | ✅ Düzeltildi (`alive_` atomic flag) |
| I7 | WasapiCapture shutdown UAF | ✅ Doğrulandı — daha eski bir V-planında (D16) zaten çözülmüştü, yinelenen madde |
| I8 | WS auth yok (gerçek açık: legacy `{cmd}` yolu) | ✅ Düzeltildi (11.07, 7 commit b00116d..da843fd — oturum-düzeyi obs-websocket auth, legacy yol dahil; eski token+Origin çürütüldü). Tarayıcı davranış onayı kullanıcıda |
| I9 | `CoUninitialize()` koşulsuz | ✅ Düzeltildi (11.07, cdb9dcb — üretimde tek konum command_router; SUCCEEDED guard) |
| I10 | SEH filtreleri AV/stack overflow yutuyor | ✅ Düzeltildi (11.07, 4 commit — paylaşımlı seh_filter.{h,cpp}: SO/BP/SS pass-through + AV görünürlük + eskalasyon valfi/__fastfail. Plan step-3 CONTINUE_SEARCH çürütüldü) |
| I11 | Çift action-queue consumer race | ✅ Düzeltildi (11.07, I33 serisi — iki-kuyruk mimarisi: aktüatör + ayrı UI event kuyruğu) |
| I12 | MainWindow yıkım sırası | ✅ Düzeltildi (referans koparma) |
| I13 | İlk kare sıralaması | ✅ Doğrulandı — zaten doğru gate'li |
| I14 | `rj_metrics_poll` implemente değil | ⏳ Açık |
| I15-I18 | Sprint 3 (performans/mimari) | ⏳ Hiç dokunulmadı |
| I19 | HEALING_MODE semantiği 4 katmanda farklı | ✅ Düzeltildi (enum 4 varyanta genişletildi, + gerçek kök neden bulundu ve düzeltildi: C++ wiring boşluğu, aşağıya bakın) |
| I20 | `evaluate_adaptive()` donmuş `self.mode` okuyor | ✅ Düzeltildi (canlı HEALING_MODE okuyor, `self.mode` alanı kaldırıldı) |
| I21-I26 | Sprint 4 (temizlik) | ⏳ Hiç dokunulmadı |
| I27 | ITransport SEH virtual-call riski | ✅ Düzeltildi (`noexcept` + iç try/catch) |
| I28 | `oldLayout=UNDEFINED` validation | ✅ Kapandı — gerçek dual-GPU donanımda doğrulandı, kasıtlı tasarım |
| I29 | Keyed mutex yanlış memory nesnesi | ✅ Çürütüldü — tek import 3 slota alias, sorun yok. Komşuda I32 bulundu |
| I30 | Cross-adapter'da KEYEDMUTEX flag eksik | ✅ Kapandı — flag eklenmedi (zaten root-cause değil), ölü kod temizlendi |
| I31 | BGRA/RGBA format tutarsızlığı | ✅ Çürütüldü — haritalandı, defekt yok |
| I32 | `invalidate_pool()` üçlü-free | ✅ Düzeltildi (kritik, gerçek memory corruption riski) |
| I33 | CoPilot onay kapısı uçtan uca yok (stub'dan fazlası) | ✅ Düzeltildi (11.07, 7 commit df1c163..b20608f — pending deposu/reject cooldown/auto-onay motorda; alt maddeler I33a/b/c). GUI davranış onayı kullanıcıda |

**V8'e önemli bir ek not:** Sprint 1'in "I2+I3 tek paket" varsayımı çürütüldü — bkz. bölüm 4, WGC/DXGI keşfi.

---

## 3. En Son Kapanan İş — I19/I20/I11 (Bu Oturumun Sonu)

**Commit'ler (hepsi master'da, push edildi, 2026-07-11 tarihli):** `e3de27f`, `f7727d7`, `f738d93`, `231e447`.

Ne yapıldı:
1. **HealingMode enum'u 3 varyanttan 4'e genişletildi** (AutoPilot/CoPilot/Assist/Manual) — çünkü UI (`settings_dialog.cpp`) gerçekten 4 farklı anlamlı seçenek sunuyordu, Rust tarafı bunları yanlışlıkla 3'e sıkıştırıyordu (eski `ManualAssist`, Assist+Manual'i tek varyanta çöküyordu).
2. **Kritik kök neden bulundu:** `healingModeChanged` sinyali sadece UI-yerel `healing_overlay_->setHealingMode()`'u çağırıyordu, Rust'a hiç iletilmiyordu (`rj_set_healing_mode()` üretim kodunda hiç çağrılmıyordu). Yani kullanıcının Settings'te seçtiği hiçbir mod motoru etkilemiyordu — motor kalıcı olarak AutoPilot'ta donmuştu. Bu düzeltildi (2 handler + startup senkronu).
3. **Kullanıcıya görünen 2 davranış değişikliği (dürüstçe belgelendi):**
   - **Manual** artık gerçekten Assist'ten farklı (hiç otomatik aksiyon yok; RuleEngine hiç çağrılmaz).
   - **Varsayılan başlangıç modu AutoPilot → CoPilot** (UI'nin varsayılanıyla tutarlı hale getirildi; startup'ta self-healing artık otomatik değil, onaya sunuluyor).

**Kalan manuel adım (kullanıcıda):** Settings'te Manual seçip OK'e basıp `rj_get_healing_mode()==3` döndüğünü ve kritik olmayan bir eşiği aşıp hiçbir aksiyon tetiklenmediğini GUI'de gözlemlemek. Build+link+FFI roundtrip + enum eşlemesi zaten otomatik doğrulandı; sadece görsel/davranışsal GUI onayı eksik.

**I11 (aynı oturumda araştırıldı, kod değişmedi):** Race doğrulandı ama planın "C++ thread'i kaldır" varsayımı yanlış çıktı — iki tüketici farklı, gerçek amaçlara sahip: `command_router.cpp::action_thread_main()` (100ms) = aktüatör (encoder'a bitrate/res/fps uygular); `main_window.cpp::pollHealingActions()` (200ms) = UI görünümü (CoPilot onay/undo). İkisi de gerekli → derin yeniden tasarım I33 (CoPilot onay akışı) ile birlikte ele alınacak, kullanıcı onayına sunulacak.

---

## 4. Bu Oturumun En Büyük Keşfi — WGC/DXGI Topoloji Gerçeği

V8'in I2/I3'ünü incelerken, projenin GPU topoloji zihinsel modelinin büyük ölçüde yanlış olduğu ortaya çıktı:

**Eski (yanlış) varsayım:** Capture = AMD (DXGI Desktop Duplication), Encode = NVIDIA, aralarında cross-adapter Vulkan zero-copy transfer.

**Gerçek (kanıtlanmış) runtime davranışı:**
```
WGC(NVIDIA texture) ─┬─► ENCODE: doğrudan NVENC (NVIDIA)      [zero-copy, AMD'ye hiç uğramıyor]
                      └─► PREVIEW: NVIDIA staging → CPU → AMD GL  [CPU-bounce]

KULLANILMAYAN (inert, DXGI fallback'e özel): transfer(), external_memory_bridge,
AMD Vulkan keyed-mutex copy_optimizer zinciri
```
`WgcScreenCapture::is_supported()` Win11'de her zaman `true` döndüğü için DXGI yolu fiilen hiç seçilmiyor — sadece WGC başarısız olursa devreye girer (nadiren).

**Sonuçları:**
- `.claude/skills/vulkan-interop-debug/SKILL.md` tam revize edildi — artık "Aktif yol — WGC" ve "Fallback yol — DXGI" olarak doğru ayrılmış.
- I2/I3 "yanlış konumlanmış"/"konum değişti" olarak kapatıldı — DXGI fallback için kod hâlâ geçerli ve önemli, ama şu an aktif olarak çalışmıyor.
- Cross-vendor (AMD+NVIDIA) zero-copy paylaşımının endüstri çapında bilinen, çözülmemiş bir D3D/hibrit-GPU sınırlaması olduğu dış araştırmayla (OBS Studio'nun kendi belgeleri dahil — "OBS can only run on one of these GPUs") doğrulandı ve kapatıldı — bu yönde bir daha araştırma yapılmayacak (hafızaya da işlendi).
- I4/I5/I27/I28/I30/I32'de yapılan düzeltmeler boşa gitmedi — DXGI fallback koduna ait, WGC arızalanırsa veya farklı donanımda devreye girerse hâlâ değerli.

---

## 5. Şu An Nerede Kaldık — Sıradaki Somut Adımlar

Hiçbir acil/bloke eden iş yok. **I33+I11 (CoPilot onay kapısı) ve I8 (WS auth) 11.07'de tamamlandı** (bkz. bölüm 2 tablosu + `SESSION_NOTES.md` 11 Temmuz). Aşağıdakiler, öncelik sırasına göre makul sonraki adımlar — hangisiyle devam edileceği kullanıcı tercihine bağlı:

1. **I9, I10, I14** — Sprint 2'nin kalan düşük-orta öncelikli maddeleri.
2. **Sprint 3-4 (I15-I18, I21-I26, + I34 inert checkbox)** — performans/mimari tutarlılık/temizlik, hiç dokunulmadı, çoğu düşük efor.
3. **Faz 3 — Çoklu Kaynak Mimarisi (ISource)** — ROADMAP'teki bir sonraki büyük faz, hiç başlanmadı. (Sonrasında Faz 4 — NDI, Faz 5 — Zig global state tam çözümü.)

**Kullanıcının elinde bekleyen (Claude Code otonom yapamıyor):**
- **WS auth tarayıcı doğrulaması (I8'i tam kapatır)** — control.html parolalı (prompt→bağlan / yanlış→4009 hata+retry) ve parolasız akış; opsiyonel: obs-websocket-js/simpleobsws ile parolalı bağlantı
- **CoPilot onay/reddet GUI doğrulaması (I33'ü tam kapatır)** — CoPilot'ta gerçek eşik aşımı → pending görünür → onayla (uygulanır) / reddet (uygulanmaz + 120s bastırılır) / timeout (geçersizleşir, cooldown yok)
- Twitch/YouTube gerçek ingest testi (Faz 2'yi tam kapatır)
- Manual healing mode GUI doğrulaması (I19/I20'yi tam kapatır)
- I32'nin validation-layer double-free VUID karşılaştırması (opsiyonel, ek kanıt)
- I30'un run.log davranış teyidi (opsiyonel, ek kanıt)
- Fiziksel Stream Deck/Companion donanım testi (opsiyonel, Faz 1'i tam kapatır)

---

## 6. İncelenmesi/Referans Alınması Gereken Dosyalar

| Dosya | Neden önemli |
|---|---|
| `docs/FABLE5_BUG_PLAN_V8.md` | Kanonik bug takip belgesi — her yeni V8 işi buraya işlenmeli |
| `docs/SESSION_NOTES.md` | Oturum günlüğü — çok uzun, en son bölümler en güncel bağlamı taşıyor |
| `docs/ROADMAP.md` | Faz 0-5 durumu, dürüst niteliklerle |
| `docs/FFI_CONTRACT.md` | Rust/C++ FFI sınırının resmi sözleşmesi (RjCommand 24B / RjAction 20B / RjMetricSample 64B, 13 `extern "C"` fonksiyon, `catch_unwind` korumalı) |
| `.claude/skills/vulkan-interop-debug/SKILL.md` | Bu oturumda tam revize edildi — WGC/DXGI ayrımı, güncel |
| `.claude/skills/ffi-safety-review/SKILL.md` | FFI/ABI kuralları + "yokluk iddialarında find-references kullan" kuralı |
| `.claude/skills/obs-ws-protocol/SKILL.md` | Faz 1 kapsam takibi, msgpack notu |
| `docs/talimatlar/` | Geçmiş görev talimatlarının arşivi (README.md ile) — tarihsel referans, canlı doküman değil |
| `src/orchestrator/src/healing.rs` | I1/I19/I20 ile en son değişen dosya — HealingMode, RuleEngine entegrasyonu |
| `src/pipeline/capture/capture_dxgi.cpp` | Gerçek konum netleşen I3'ün (artık kapalı) yaşadığı dosya |
| `src/pipeline/gpu/vulkan_initializer.zig` | `use_keyed_mutex` kararının verildiği yer |

---

## 7. Dış Araçlar

- **Linear** — `linear.app/reji-studio` — issue/sprint takibi (REJ-5 ila REJ-13)
- **Notion** — "Reji Studio — Proje Merkezi" sayfası — genel proje referansı
- **Todoist** — "Reji Studio" projesi — günlük açık kalem takibi
- **GitHub** — `github.com/rejistudio-official/reji-studio`, Claude Code üzerinden yönetiliyor
- ~~Telegram bridge~~ — kaldırıldı (kullanılmıyor)

---

## 8. Küçük Açık Kalemler / Teknik Borç (V8 dışı)

- **Audio metrikleri izlenmiyor** — WASAPI tarafı self-healing için kör nokta (ses tabanlı kural yazılamaz).
- **`default_mode` alanı** `rules.json`'da parse ediliyor ama kullanılmıyor.
- **Zig modül-global state** (`external_memory_bridge.zig`, `vulkan_initializer.zig`) — çoklu instance senaryosunda gerçek çözüm gerekiyor (Faz 5; geçici double-init uyarısı mevcut).

---

## 9. Genel Çalışma Disiplini (bu oturumda kurulan)

- Her commit küçük, mantıksal olarak bölünmüş, push öncesi onay bekleniyor.
- Eski raporlara/varsayımlara körü körüne güvenilmiyor — her V8 maddesi talimat yazılmadan önce güncel master'a karşı yeniden doğrulanıyor. Bu disiplin defalarca gerçek fayda sağladı (I7 yinelenen çıktı, I29/I31 çürütüldü, WGC keşfi I2/I3'ü tamamen yeniden çerçeveledi).
- `tests/baseline_metrics.txt` karakterizasyon test artefaktı — asla commit edilmiyor (hafıza kuralı).
- Donanım/GUI gerektiren testler kullanıcıya bırakılıyor, Claude Code otonom olarak zorlamıyor.
- **Dürüstlük ilkesi:** "test edildi" ile "kod incelemesiyle doğrulandı" ile "muhakemeyle kabul edilebilir" arasındaki fark her zaman açıkça belirtiliyor.
