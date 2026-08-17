# TASARIM: VFR/CFR — Faz 1 (Kare Tekrarı ile CFR Üretimi)

**Tarih:** 2026-08-17 · **Talimat:** `TALIMAT_VFR_CFR_FAZ1.md` ·
**Girdi:** `BULGULAR_VFR_CFR_FAZ0.md` · **Kod yazılmadı — onay bekliyor.**

---

## K1 — Texture kopyalama stratejisi

**Karar: WGC yolunda her geçerli karede pipeline'a ait texture'a
`CopyResource`; encoder'a HER ZAMAN bu kopya verilir (canlı pool
texture'ı değil). DXGI yolunda ek kopya yok.**

Gerekçe:

- **"Yalnız kopya yoksa kopyala" seçeneği uygulanamaz.** Null tick'te
  kopyalanacak kaynak yok: WGC `next_frame()` null döndürmeden ÖNCE
  önceki texture'ı bırakıyor (`capture_wgc.cpp:171-176`). Sonraki
  tick'in null olacağını önceden bilemeyiz → kopya geçerli karede,
  her seferinde alınmak zorunda. Soru "her karede mi / bazen mi"
  değil; kopya kaçınılmaz, tek karar "encoder neyi görecek".
- **Encoder'ın hep kopyayı görmesi gizli bir maliyeti de kaldırıyor.**
  `ensure_registered()` tek slotlu pointer cache
  (`encode_nvenc.cpp:285-292`: farklı pointer → unregister +
  re-register). WGC frame pool 2 buffer'lı (`capture_wgc.cpp:146-150`)
  → bugün pool pointer'ları dönüşümlü geldikçe **her karede** NVENC
  kayıt döngüsü dönüyor olabilir (encP içinde gömülü; büyüklüğü
  **doğrulanmadı**, Faz 2'de copy=/encP kıyasıyla görülür). Sabit
  kopya texture'ı ile kayıt oturum başına gerçekten bir kez olur.
- **Kopya nerede:** `Pipeline::Impl`'de `ComPtr<ID3D11Texture2D>
  repeat_tex_` (+ desc cache). Lazy yaratım: kaynak texture'ın
  cihazından (`tex->GetDevice()` — `emit_wgc_preview`'daki kalıp,
  `existing_desktop_source.cpp:127-134`), boyut/format değişiminde
  reset + yeniden yaratım (aynı dosyadaki çözünürlük-değişimi kalıbı).
  Desc: `USAGE_DEFAULT`, `BIND_RENDER_TARGET | BIND_SHADER_RESOURCE`.
  **Doğrulanmadı:** NVENC'in app-yaratımı bu texture'ı register
  kabul şartları (bind flag kombinasyonu) — Faz 2'nin İLK dikey
  diliminde deneyle kesinleştirilecek.
- **Maliyet:** `CopyResource` submit-only (~GPU'da async, CPU'da
  <0,5 ms bekleme yok — preview ringinde öğrenilen ders). `[SendDiag]`
  bütçesi `tot` 6,7-8,8 ms / 16,7 ms → yer var; yeni `copy=` metriği
  (K5) gerçek maliyeti ölçer, kabul kriteri `tot < 16.7ms` korunur.
- **DXGI yolu:** Zaten kararlı `shared_texture_`'a kopyalıyor
  (`capture_dxgi.cpp:383-413`) — son kare null iterasyonda da hayatta,
  NVENC kaydı zaten stabil. Ek kopya işlevsiz +0,5 ms olurdu → DXGI'de
  tekrar, en son encode edilen handle (kararlı `shared_texture_`) ile
  yapılır. **Doğrulanmadı:** tekrar encode'un keyed-mutex/Vulkan
  interop ile etkileşimi — Faz 2'de kontrol; sorun çıkarsa WGC ile
  aynı kopya yoluna düşülür (tek satırlık fark).

## K2 — Tekrar tetikleme mantığı

**Karar: Her null tick'te tekrar — eşik yok, kadans yok, üst sınır
yok (YAGNI). Kaynak `NeedsReinit` durumunda da tekrar sürer.**

Gerekçe:

- Amaç CFR üretmek; "akıllı" tekrar (yalnız GOP dolarken / en fazla N
  ardışık) VFR'yi kısmen geri getirir ve tam da en kritik senaryoda
  (uzun statik BRB ekranı) Faz 0'daki üç sorunun üçünü de (GOP uzaması,
  ses durması, VFR) yeniden açar. N-sınırı, çözdüğümüz hatayı sınırın
  ötesinde aynen yeniden üretir.
- Basit kural öngörülebilir ve test edilebilir: beklenen davranış
  "her tick'te tam bir kare encode edilir" — `dup + frames ≈ fps`
  invariantı 1 Hz penceresinde assert edilebilir.
- Bant genişliği maliyeti skip-MB varsayımına dayanıyor
  (**ölçülmedi** — Faz 2 doğrulama adımı). Ölçüm beklenenden kötü
  çıkarsa kadans kararı o veriyle yeniden açılır; bugün spekülatif
  optimizasyon yapılmaz.
- **NeedsReinit'te tekrar sürer:** yayın kopmasın — recovery banner /
  reinit süresince izleyiciye son kare + kesintisiz ses gider. Reinit
  akışının kendisi (`RecoveryCoordinator`) değişmez; tekrar yalnızca
  "kopya mevcutsa" koşuluna bağlı. İlk geçerli kareden önce kopya
  yoktur → null tick bugünkü gibi boş geçer (yayın zaten IDR'sız
  başlayamaz, davranış değişmez).
- Tekrar karesi drop DEĞİL: `frame_drop_policy.h` outcome'ları
  değişmez; `n_null` sayacı da aynen kalır (tekrar, null iterasyonun
  İÇİNDE ek iştir — sınıflandırması değişmez).

## K3 — GOP'u zaman bazlı yapmak

**Karar: Evet — savunma katmanı olarak zaman bazlı IDR eklenir:
"son IDR'dan ≥2 sn geçtiyse `request_idr()`".**

Gerekçe:

- Kare tekrarı GOP'u dolaylı düzeltir (120 kare ≈ 2 sn @60fps) ama bu
  garanti, tekrar mekanizmasının kusursuzluğuna bağlı. İki bağımsız
  hata modu kalır: (a) tekrar yolunda beklenmedik duraksama,
  (b) pacing'in resync'i (`frame_pacer.cpp:46-47`) sonrası kadans
  kayması. Zaman bazlı IDR ikisini de örter; platform sınırı (2 sn)
  mekanizmadan bağımsız korunur.
- Maliyet çok düşük: `request_idr()` mevcut (`encode_subsystem.h:51`),
  IDR gerçekleşme bilgisi mevcut (`on_packet`'te `pkt.is_keyframe`,
  `encode_nvenc.cpp:395-396`). Saf politika ("şimdi IDR iste?") tek
  başlık dosyasında test edilir.
- `gopLength=120` birincil mekanizma olarak kalır; zaman bazlı IDR
  yalnız gecikince devreye girer (normal akışta hiç tetiklenmemeli —
  bu da testte assert edilir).
- Bilinen maliyet: statik içerikte her ~2 sn'de bir IDR, skip-MB
  P-frame'lerden büyüktür → statik sahnenin bitrate tabanını IDR'lar
  belirler (**ölçülmedi**, Faz 2 doğrulama adımına dahil).

## K4 — Ses drain kadansı

**Karar: Bağımsız ses drain refactor'u YAPILMAZ. Kare tekrarı (K2:
her null tick) drain kadansını kendiliğinden düzeltir.**

Gerekçe:

- K2 "her null tick'te tekrar" seçildiği için her tick'te
  `encode_frame()` → `drain_one()` → `on_packet` →
  `audio_bridge_.drain()` zinciri çalışır (`encode_nvenc.cpp:391-401`,
  `pipeline.cpp:355-360`). Drain kadansı tick frekansına (60 Hz)
  kilitlenir — A5 çözülür.
- Zincirdeki tek koşul `bitstreamSizeInBytes > 0`
  (`encode_nvenc.cpp:391`): senkron modda her encode'un paket ürettiği
  varsayımı **çıkarım, doğrulanmadı** — Faz 2'de `dup` sayacı ile
  `n_senda` kıyaslanarak doğrulanır. Varsayım çökerse (NVENC boş paket
  dönerse) o zaman B planı devreye girer: `drain()` çağrısını
  `on_packet`'ten `run_frame` gövdesine taşımak (tek thread aynı —
  RTMP tek-yazar invariantı bozulmaz). Bu B planı bilinçli olarak
  şimdi YAPILMIYOR (YAGNI): kanıt olmadan hot-path'te ikinci bir
  değişiklik yüzeyi açmamak için.
- Kalan bilinen boşluk: ilk geçerli kareden önce / encoder yokken ses
  yine akmaz — bugün de böyle, kapsam dışı (kayda geçirildi).

## K5 — Ölçülebilirlik

**Karar: `[SendDiag]`'a üç ekleme: `dup=` (penceredeki tekrar kare
sayısı), `copy=avg/max ms` (CopyResource submit süresi), `idrF=`
(zaman bazlı yedek IDR tetik sayısı — normalde 0 beklenir).**

Gerekçe:

- `dup`: Faz 2 kabulünün ana sinyali — `frames + dup ≈ fps` ve
  `wire_fps ≈ fps` birlikte CFR'nin kanıtı.
- `copy`: hot-path bütçe takibi; `tot < 16.7ms` kabul kriterinin
  bileşeni. Kopya maliyeti görünmezse regresyonu da görünmez olur
  (bugünkü `wire_fps` dersinin aynısı).
- `idrF`: savunma katmanının "sessizce ana mekanizma oluvermesi"ni
  yakalar — `idrF > 0` sürekli görülüyorsa tekrar mekanizması aksıyor
  demektir; bu, gelecek regresyonların erken uyarısı.
- Mekanik: `SendDiagStats`'a 2 sayaç + 1 `Acc`; `format_send_diag`'a
  `dup=%u copy=%.1f/%.1fms idrF=%u`. Saf başlık — birim testi doğrudan.

---

## Dokunulacak Dosyalar + Tahmini Boyut

| Dosya | Değişiklik | ~Satır |
|---|---|---|
| `src/pipeline/include/frame_repeat_policy.h` (YENİ) | Saf politika: tekrar kararı (kopya var mı × null × state) + IDR kadans sayacı ("son IDR pts'inden ≥2 sn?") | 50-70 |
| `src/pipeline/pipeline.cpp` | Impl'e `repeat_tex_` (ComPtr + desc), geçerli karede copy + encode-girdisini kopyaya çevirme (WGC), null dalında tekrar encode, IDR kadans kontrolü, SendDiag beslemeleri | 60-90 |
| `src/pipeline/include/send_diag.h` | `dup/copy/idrF` alanları + format | 20-30 |
| `tests/` (mevcut kalıba uygun) | `frame_repeat_policy` birim testleri + send_diag format testi | 80-120 |
| **Toplam** | | **~210-310** |

Dokunulmayan: `encode_nvenc.*` (mevcut API yeterli), `rtmp_transport.zig`
(timestamp'ler zaten pts'ten), `audio_*` (K4), `frame_pacer.*`,
`capture_*` (kopya pipeline katmanında — kaynak sözleşmesi değişmez).

## Faz 2 Commit Sırası (her commit derlenir + çalışır, additive-önce)

1. **`feat: SendDiag dup/copy/idrF alanları`** — sayaçlar + format +
   birim test. Üretici yok, hepsi 0 raporlar. Davranış değişikliği sıfır.
2. **`feat: frame_repeat_policy saf çekirdek`** — politika başlığı +
   birim testleri. Henüz çağıran yok. Davranış değişikliği sıfır.
3. **`feat: WGC encode girdisi kalıcı kopyaya taşındı`** — geçerli
   karede `CopyResource` + encoder'a kopya. Dikey dilim: NVENC'in
   app-texture register kabulü BURADA kanıtlanır (K1 doğrulanmamış
   noktası). `copy=` dolmaya başlar, `dup=` hâlâ 0. Tek başına da
   değerli (kayıt thrash'i biter). Riskli tek commit bu → küçük tut.
4. **`feat: null tick'te kare tekrarı`** — davranışsal çekirdek.
   `dup=` saymaya başlar, ses kadansı düzelir, CFR oluşur.
5. **`feat: zaman bazlı yedek IDR`** — kadans politikası bağlanır,
   `idrF=` raporlanır.
6. **`docs: ölçüm sonuçları + ROADMAP/Todoist güncelleme`** — bitrate
   ölçümü (skip-MB varsayımı doğrulama), `[SendDiag]` önce/sonra
   karşılaştırması, kalan riskler.

Geri alma kolaylığı: 4 ve 5 bağımsız revert edilebilir; 3'ün reverti
2-1'i etkilemez.

## Test Planı

**Birim (mevcut gtest kalıbı):**
- `frame_repeat_policy`: (kopya yok → tekrar yok), (kopya var + null →
  tekrar), (NeedsReinit + kopya var → tekrar), (geçerli kare → tekrar
  yok + kopya tazelenir işareti); IDR kadansı: 2 sn sınır koşulu,
  tetik sonrası sayaç sıfırlama, normal kadansta hiç tetiklenmeme,
  çifte-istek üretmeme.
- `send_diag`: yeni alanların pencere agregasyonu + format satırı.
- Regresyon: `frame_drop_policy` testleri değişmeden geçmeli
  (tekrar ≠ drop sınıflandırması).

**Entegrasyon (yerel, kullanıcısız):**
- Statik masaüstü + 25 fps video senaryoları ile çalıştır;
  `[SendDiag]` kabul kriterleri: `wire_fps ≈ 60`, `frames + dup ≈ 60`,
  `tot_max < 16.7ms`, `copy_avg < 1ms`, `idrF = 0`, `n_senda` sürekli.
- Çıkışı ffmpeg/ffprobe ile dinle: kare aralıkları sabit (~16,7 ms),
  keyframe aralığı ≤ 2 sn, statik sahnede ölçülen bitrate kaydedilir
  (skip-MB varsayımı doğrulama adımı).

**Canlı doğrulama (kullanıcıda kalır):**
- Twitch'e 5-10 dk test yayını: ilk yarısı hareketli içerik, ikinci
  yarısı tam statik BRB ekranı. Twitch Inspector'da: FPS sabit mi,
  keyframe uyarısı var mı, statik bölümde bağlantı/ses kesintisi
  oluyor mu? (Faz 0'ın "doğrulanmadı" bıraktığı gerçek platform
  davranışı ancak burada kapanır.)

## Risk Notu

**Bu değişiklik `run_frame` hot-path'ine dokunuyor — preview darboğazı
tam burada bulunmuştu.** Riskler ve önlemler:

1. **Kopya maliyeti hot-path'i şişirir.** Önlem: `CopyResource`
   submit-only (bloklu Map/bekleme YOK — preview ringinin dersi);
   `copy=` metriği 1. günden ölçer; kabul kriteri `tot < 16.7ms`
   commit 3'te, davranış değişikliğinden (commit 4) ÖNCE doğrulanır.
   Aşarsa commit 4'e hiç geçilmez.
2. **Encode girdisi değişiyor (commit 3) — NVENC register reddi veya
   görsel bozulma olasılığı.** Önlem: dikey dilim ilk denemede bunu
   kanıtlar; başarısızsa alternatif desc/bind bayrakları denenir,
   olmadı → tasarıma dönülür (kopyayı NVENC'e değil, tekrar anında
   canlı yola vermek gibi B planları o veriyle tartılır).
3. **Tekrar + preview etkileşimi.** Null tick'te preview yolu bugün
   de çalışmıyor; tasarım bunu DEĞİŞTİRMİYOR (tekrar yalnız encode
   besler). `prev`/`n_prev_miss` regresyon nöbetçisi.
4. **Statik sahnede 2 sn'de bir zorunlu IDR bitrate tabanı oluşturur.**
   Önlem: commit 6'da ölçüm; sorunsa IDR kadansı 4 sn'ye (YouTube
   sınırı) gevşetilebilir — politika sabiti tek yerde.
5. **DXGI yolunda tekrar + keyed-mutex/Vulkan interop yarışı
   (doğrulanmadı).** Önlem: commit 4'te DXGI topolojisi ayrıca
   koşulur; sorun görülürse DXGI de WGC'nin kopya yoluna alınır
   (tasarımda tek satırlık fark olarak öngörüldü).
6. **Genel emniyet:** her commit bağımsız revert edilebilir; `dup`
   mekanizması tek koşula bağlı olduğundan acil durumda commit 4
   reverti sistemi bugünkü (bilinen) VFR davranışına döndürür.

## Doğrulanmamış Noktalar (Faz 2'de kapanacak)

- NVENC'in app-yaratımı texture'ı register kabul şartları (commit 3).
- "Senkron modda her encode paket üretir" varsayımı (K4 — `dup` vs
  `n_senda` kıyası).
- Skip-MB P-frame bitrate maliyeti + 2 sn IDR'ın statik sahne bitrate
  tabanı (commit 6 ölçümü).
- Bugünkü WGC yolunda per-frame register/unregister maliyetinin gerçek
  büyüklüğü (commit 3 önce/sonra `encP` kıyası).
- Gerçek platform davranışı (kullanıcı canlı testi).

---

**Onay bekliyor. Faz 2 (implementasyon) yalnız onaydan sonra.**
