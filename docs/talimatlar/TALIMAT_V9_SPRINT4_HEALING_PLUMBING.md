# TALİMAT: V9 Sprint 4 (J14-J16) + Healing Plumbing (HP1-HP4)

**Kaynak:** `docs/FABLE5_BUG_PLAN_V9.md` (J14-J16), `docs/SESSION_NOTES.md`
("healing plumbing gözlemleri" notu — J9/J10 Faz 0 keşiflerinden).
**Hedef dosya konumu:** Tamamlanınca `docs/talimatlar/` arşivine taşınmalı.

---

## Görevin Özü

İki farklı kaynaktan gelen, ama tek oturumda ele alınacak iki iş paketi:

**Bölüm A — Sprint 4 (J14-J16):** V9 planının düşük öncelikli/bakım
maddeleri. Küçük, çoğunlukla bağımsız, tasarım turu gerektirmiyor.

**Bölüm B — Healing Plumbing (HP1-HP4):** V9'un üç-model taramasının
**hiç görmediği** bir alan — J9/J10'un Faz 0 keşifleri sırasında tesadüfen
ortaya çıktı. Resolution-healing (termal/RAM bazlı çözünürlük koruması)
fiilen çalışmıyor ve `gpu_thermal_restore` kuralının koşulu (metrik stub
nedeniyle) hep-doğru kalıp CoPilot'ta yanıltıcı/temelsiz onay istekleri
üretebiliyor. **Aciliyet düşük** (stream-kritik bitrate-healing katmanı
sağlam, bu ikincil bir koruma), **ama önemi düşük değil** — CoPilot'un
güvenilirliğini sinsice aşındıran bir semptom (alarm yorgunluğu riski).

Bölüm B, Bölüm A'dan daha fazla dikkat gerektiriyor çünkü dört bulgu
birbirine bağlı ve tek bir tutarlı akışın parçaları — ayrı ayrı değil,
birlikte tasarlanmalı.

Sıra önerisi: **Bölüm A önce** (hızlı, çoğu bağımsız), **sonra Bölüm B**
(daha fazla tasarım gerektiriyor, kendi Faz 0/1 döngüsü hak ediyor).

---

# BÖLÜM A — Sprint 4 (J14-J16)

## J14 — Kimlik bilgileri düz metin (WS parola, RTMP key)

**V9 kaynağı:** Fable5 6.3, GLM 6.3 (2/3) — **bilinen/kabul edilmiş
sınırlama, düşük öncelik**

### Faz 0
1. `settings_dialog.cpp`'de `QSettings` üzerinden kaydedilen WS parolası
   ve RTMP key'i bul.
2. I8 sırasında bu konunun zaten değerlendirilip "OBS ile aynı yaklaşım"
   gerekçesiyle kabul edilmiş bir tasarım kararı olduğunu teyit et (kod
   içi yorum var mı, SESSION_NOTES'ta geçiyor mu).

### Faz 1 (karar — muhtemelen "dokunma" veya opsiyonel)
Bu madde, J7/J10'daki gibi "gerçek ama kasıtlı" kategorisine yakın. İki
seçenek:
- **(a) Dokunma** — zaten bilinçli kabul edilmiş bir sınırlama, V9'da
  "gözden geçirildi, mevcut kararla tutarlı" notuyla kapat.
- **(b) DPAPI sarmalama** — `CryptProtectData` ile defense-in-depth,
  opsiyonel. Yalnızca kullanıcı bunu gerçek bir tehdit olarak görürse.

**Varsayılan öneri: (a).** Kapsamı büyütme, onay olmadan (b)'ye geçme.

### Faz 2-3 (yalnızca (b) seçilirse)
Küçük, izole değişiklik. Kalıcı ayarların formatı değişeceğinden geriye
dönük uyumluluk (eski düz-metin kayıtların okunabilmesi) değerlendirilmeli.

---

## J15 — `program_widget.cpp` hot-path'te `convertToFormat` alloc

**V9 kaynağı:** Fable5 2.2 (1/3, "hot-path'te alloc yok" yorumuyla
doğrudan çelişiyor olması dikkat çekici)

### Faz 0
1. `src/ui/program_widget.cpp`, `paintGL()`'i bul.
2. Her karede gerçekten bir `QImage::convertToFormat` alloc'u olduğunu
   teyit et — boyutu (V9 raporunda ~8MB deniyor) ve sıklığını (her frame
   mi, throttle'lı mı) doğrula.
3. Hemen üstündeki "hot-path'te alloc yok" yorumunun gerçekten bu koda mı
   ait olduğunu, yoksa başka bir bloğa mı ait olduğunu kontrol et (yorum
   yanlış yerde olabilir).
4. `PreviewWidget`'ın zaten `GL_BGRA` ile doğrudan yüklediğini (V9'un
   önerdiği çözüm modeli) teyit et — aynı deseni `ProgramWidget`'a
   uygulamak mümkün mü, yoksa `ProgramWidget`'ın farklı bir kısıtı var mı
   (örn. farklı format ihtiyacı).

### Faz 1 (basit, hızlı onay yeterli)
Faz 0 doğrularsa: BGRA'yı `GL_BGRA` ile doğrudan yükle (shader swizzle
ile, `PreviewWidget` deseni) veya dönüşümü tek seferlik `uploadFrame`'e
taşı. Küçük, cerrahi.

### Faz 2-3
Regresyon: preview görüntüsünün doğru render edildiğini (renk kanalları
doğru) kod incelemesiyle + varsa görsel testle doğrula. GUI görsel
onayı muhtemelen kullanıcıda kalacak (renk doğruluğu).

---

## J16 — Küçük maddeler (gruplandırılmış)

**V9 kaynağı:** çeşitli, hepsi 1/3, düşük risk/düşük etki

Her biri için hafif Faz 0 (kod konumunu doğrula, gerçekten geçerli mi
kontrol et), sonra düşük riskliyse doğrudan düzelt:

1. **`create_cross_adapter_shared()`'da `OpenSharedResource1` başarısız
   olursa handle sızıntısı** (GLM 2.1) — J3 ile aynı dosya/bölge
   (`gpu_resource_manager.cpp`). J3'ün fail-closed değişikliğiyle
   birlikte gözden geçir, aynı yerdeyken düzelt.
2. **`FrameProfiler` her mark çağrısında mutex alıyor** (GLM 4.4) —
   60fps'te 6 kilit/kare. Yalnızca tanı-amaçlı kod etkileniyor mu teyit
   et (production path'i etkilemiyorsa öncelik düşük, opsiyonel).
3. **`preview_staging_` legacy texture hâlâ her karede kopyalanıyor**
   (Fable5 4.2, Opus 2.4) — deprecated işaretli ama aktif kullanılıyor
   mu teyit et. **Dikkat: J11'in Faz 0'ı bu alanı zaten incelemişti**
   (`preview_staging_` frame-thread-only olduğu teyit edilmişti) — o
   bulguyu tekrar keşfetme, doğrudan referans al.
4. **`next_action_id()` wrap'ta teorik 0-sentinel çakışması** (Fable5
   1.7, Opus 1.6) — I23'teki u32-global ID kararıyla aynı analiz sınıfı
   (ama farklı sayaç — bunu I23/J4 ile karıştırma, ayrı bir
   `next_action_id()` fonksiyonu). ~13 yıl kesintisiz çalışmada
   gerçekleşir, pratik önemi yok. `fetch_update` ile bir satırlık
   iyileştirme mümkünse yap, değilse "muhakemeyle kabul edilebilir,
   pratik risk yok" notuyla kapat.

**Kapsam dışı bırakılanlar (V9 planında zaten işaretli, bu talimata
dahil değil):** `MainWindow` god-object, `PreviewWidget`'ın Vulkan
submission mimarisi, derin Vulkan/GL interop bulguları (Fable5/Opus/GLM
3.x maddeleri — bunlar I23'ün devamı niteliğinde ayrı bir derin tur
gerektiriyor, üç raporun birbiriyle kısmen çeliştiği bir alan).

---

# BÖLÜM B — Healing Plumbing (HP1-HP4)

**Kaynak:** Bu talimatı yazan tarafın (sohbet Claude'u) sentezi, Claude
Code'un J9/J10 Faz 0 keşif turlarından derlendi. **V9'un üç-model
taramasının hiç dokunmadığı alan** — kendi Faz 0'ı bu talimatla
başlıyor.

## Kapsam ve Kapsam Dışı (net sınır)

**Bu turun kapsamı:** Resolution-healing'in RAM-pressure ve genel
wiring kısmını gerçekten çalışır hale getirmek.

**Kapsam DIŞI (bilinçli):** GPU-thermal metriğinin gerçek okuması
(`query_gpu_thermal_wmi`/`amd_adl`/`nvidia_nvapi` — hepsi stub, `return 0`).
Bu ayrı, potansiyel olarak büyük bir iş (gerçek WMI/ADL/NVAPI
entegrasyonu). Bu turda `gpu_thermal_restore` kuralının dormant kalması
kabul edilir — yalnızca **hep-true koşulun zararsız hale getirilmesi**
(aşağıda HP1/HP4) bu turun parçası, gerçek termal okuma değil.

## Faz 0 — Doğrulama (implementasyondan önce, zorunlu)

Dört bulgunun her biri Claude Code'un önceki keşfinde kod-doğrulanmıştı;
bu talimat implementasyona geçmeden önce **taze bir gözle kısa bir
teyit turu** istiyor (özellikle aradan geçen commit'lerin — J9, J10, Sprint 4
Bölüm A — bu alanı etkilemediğini doğrulamak için):

1. **HP1 — `RESTORE_RESOLUTION` encoder'a ulaşmıyor:** `apply_action`
   (`pipeline.cpp:776-811`) içinde `RESTORE_RESOLUTION` case'inin
   gerçekten olmadığını yeniden teyit et.
2. **HP2 — `scale_factor` okunmuyor:** `create_action` (`rules.rs:346-350`)
   yalnız `step_kbps` okuduğunu, resolution kurallarının
   `scale_factor` parametresini hiç kullanmadığını yeniden teyit et.
3. **HP3 — `set_resolution` sonucu yutuluyor:** çağrı yerini bul, dönüş
   değerinin (`bool`) gerçekten hiçbir yerde kontrol edilmediğini teyit
   et.
4. **HP4 — UI yanıltıcı state gösteriyor:** `main_window.cpp:640`'ın
   gerçek encoder durumundan bağımsız olarak "çözünürlük normale
   döndürülüyor" mesajı gösterdiğini teyit et.
5. `gpu_thermal_restore` kuralının hep-true koşulunun (`gpu_temp_c < 70`,
   metrik daima 0) hâlâ geçerli olduğunu teyit et — bu, HP4'ün neden
   önemli olduğunun kanıtı.

**Faz 0 çıktısı:** Dört bulgunun hâlâ geçerli olduğu (veya aradan geçen
işler nedeniyle değiştiği) kısa bir teyit + Faz 1 tasarımına geçiş
onayı.

## Faz 1 — Tasarım (implementasyondan önce onaya sunulacak)

Dört düzeltmenin birbirine bağımlılığını netleştir:

1. **HP2 önce mantıklı olabilir** (`scale_factor` okuma) — HP1'in
   düzeltmesi (restore wiring) test edilebilir olması için downscale'in
   de gerçekten çalışması faydalı (restore'un neyi geri aldığını
   gözlemlemek için).
2. **HP1 tasarımı:** `apply_action`'a `RESTORE_RESOLUTION` case'i eklemek
   ne yapmalı — orijinal/önceki çözünürlüğe mi dönmeli (state saklamak
   gerekir), yoksa sabit bir "varsayılan" çözünürlüğe mi? Mevcut
   `set_resolution`'ın API'sini incele, en az değişiklikle uyan yaklaşımı
   öner.
3. **HP3 tasarımı:** Yutulan `bool` sonucu nereye gitmeli — log mu
   (I10/I6 deseninde senkron ERROR log), yoksa healing motoruna geri
   bildirim mi (başarısız reconfig'i RuleEngine'e bildirmek, gelecekte
   retry/cooldown mantığı için)? **Öneri: bu turda yalnızca log yeterli**
   — geri bildirim mekanizması kapsamı büyütür, ayrı değerlendirilebilir.
4. **HP4 tasarımı:** UI mesajının gerçek sonuca bağlanması. `apply_action`
   döndürdüğü/ilettiği gerçek başarı/başarısızlık durumuna göre UI
   event'i güncellenebilir mi (I33'ün `ActionEvent` mimarisiyle uyumlu
   bir yol var mı — yeni mimari icat etme, varsa mevcut event akışını
   kullan).
5. **`gpu_thermal_restore` kuralının hep-true sorunu için minimal
   önlem:** Gerçek termal okuma bu turun kapsamı dışında, ama kuralın
   sürekli tetiklenip gereksiz pending/overlay üretmesini **azaltacak**
   ucuz bir önlem var mı (örn. metrik stub olduğu bilinen durumda kuralı
   hiç değerlendirmemek, ya da cooldown'ı belirgin uzatmak)? Bunu öner,
   ama büyük bir tasarım değişikliği gerektiriyorsa kapsam dışı bırakıp
   yalnızca not düş.

Bu tasarımı onaya sun, implementasyondan önce.

## Faz 2 — İmplementasyon (küçük, sıralı commit'ler)

Önerilen sıra (Faz 1 onayına göre uyarlanabilir):
1. HP2 — `scale_factor` okuma.
2. HP3 — `set_resolution` sonucunu logla.
3. HP1 — `RESTORE_RESOLUTION` wiring.
4. HP4 — UI'nın gerçek state'i yansıtması.
5. `gpu_thermal_restore` hep-true önlemi (Faz 1'de kapsam belirlendiyse).
6. Dokümantasyon: V9 planına healing-plumbing bulgusu resmi madde olarak
   işlenir (JP1-JP4 gibi bir numaralandırmayla veya SESSION_NOTES'taki
   notun "kapandı" olarak güncellenmesiyle), talimat arşivlenir.

## Faz 3 — Test ve Dürüstlük Sınırları

- Birim/entegrasyon: `scale_factor` okumasının doğru çalıştığını,
  `RESTORE_RESOLUTION`'ın gerçekten `set_resolution`'ı çağırdığını test
  et.
- Regresyon: `PipelineCharacterization` + mevcut healing testleri PASS
  kalmalı.
- **Gerçek runtime doğrulaması muhtemelen kullanıcıda kalacak** — RAM
  basıncı yaratıp gerçekten preview çözünürlüğünün düştüğünü/geri
  geldiğini gözlemlemek zor simüle edilebilir bir senaryo. Dürüstçe
  belirt.
- Kullanıcıya görünen davranış değişikliği listesi: RAM-pressure koruması
  artık gerçekten çalışacak (önceden sessiz no-op'tu) — bu **davranış
  değişikliği**, önceden hiçbir şey olmuyordu, artık preview kalitesi
  gerçekten düşüp geri gelecek. Bunu açıkça belirt, sürpriz olmasın.

## Sabit Kurallar (her iki bölüm için)

- Küçük, mantıksal commit'ler; her madde/alt-bölüm tamamlanınca push
  öncesi onay (kod değiştiren işler için) — saf dokümantasyon/çürütme
  commit'leri doğrudan push edilebilir (Sprint 3'te kurulan disiplin).
- `tests/baseline_metrics.txt` asla commit edilmez.
- "Test edildi" / "kod incelemesiyle doğrulandı" / "muhakemeyle kabul
  edilebilir" ayrımı her raporda açık.
- Faz 0 bulguları bu talimatın varsayımlarıyla çelişirse (özellikle
  Bölüm B'de, aradan geçen işler bir şeyi değiştirmişse) implementasyona
  geçmeden dur, raporla — kurulu proje deseni.
- Bölüm B tamamlandığında bu, **V9 bug planının fiilen tamamlanması**
  anlamına gelir (Sprint 1-4 + healing-plumbing) — CONTEXT.md ve
  istenirse dış senkron kaynaklarına (Notion/Todoist/Linear) yansıtılmalı.
