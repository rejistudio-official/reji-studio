# TALİMAT: V10 Bug Taraması — Hazırlık ve Kapsam Paketi (V9-Sonrası Yeni Kod)

**Kaynak:** V9'dan (üç-model taraması) beri eklenen büyük kod kütlesi
hiç bağımsız taramadan geçmedi. Son canlı GUI testlerinin bu yeni
bölgede üç gerçek bug bulması (hot-reload kuruluş-sırası, içe aktarım
kopyalama, dışa aktarım kör-kopyalama), yüzeyde daha fazla gizli hata
olabileceğinin sinyali. Faz 3 wiring'i bu bölgeye dokunmadan önce
temiz bir baseline kurulacak.
**Hedef dosya konumu:** Tamamlanınca `docs/talimatlar/` arşivine
taşınmalı.

---

## Görevin Özü

Bu talimat iki iş yapar:
1. **Kapsam paketini hazırla** — tarayıcı modellere verilecek dosya
   listesi + bağlam özeti (FABLE5_BUG_PLAN_V10.md'nin iskeleti).
2. **Tarama prompt'unu üret** — üç modele (kullanıcı ayrı ayrı
   çalıştıracak) verilecek, kapsamı net sınırlanmış talimat metni.

**Bu talimat tarama YAPMAZ** — tarama, kullanıcının üç ayrı modele
(önceki desenle: Fable 5, Opus 4.8, GLM 5.2 veya güncel eşdeğerleri)
bağımsız olarak yaptıracağı ayrı bir adım. Bu tur yalnızca hazırlık.

---

## Bölüm A — Kapsam Envanteri (dahil edilecekler)

V9-sonrası eklenen/büyük değişen dosyaları güncel master'a karşı
çıkar ve doğrula (aşağıdaki liste beklenen içerik — git log ile
teyit et, eksik/fazla varsa düzelt):

1. **Ses pipeline'ı (en yüksek öncelik — eşzamanlılık + COM + FFI/ABI):**
   - AAC ASC/FLV tag/PCM saf mantık başlıkları
   - MF AAC encoder (COM yaşam döngüsü, cross-thread shutdown'ın
     "belgeli kabul edilebilir risk" notu — tarayıcılar bunu bilsin)
   - SPSC audio ring + OutputSubsystem::send() drain entegrasyonu
   - rtmp_transport.zig FLV audio genişletmesi + yeni ABI yüzeyi
   - WASAPI cihaz enumerasyonu
   - A-V drift valfi
2. **Donanım Profilleme:**
   - profile_advisor.{h,cpp} (sinyal toplama + suggest_profile)
   - maybeSuggestProfileOnFirstRun + applyProfile akışı
   - Üç profil rules.json dosyası (içerik tutarlılığı: eşikler
     Faz 1 tablosuna uygun mu)
   - max_gpu_vram_mb() pipeline accessor'ı
3. **Kural yönetimi zinciri (son üç bug'ın bölgesi — özellikle dikkat):**
   - writeValidatedRules/validateRulesFile/importRules/exportRules
   - rules_watch.h + hot-reload akışı
   - rj_rules_snapshot_json FFI + Kurallar sekmesi doldurma
4. **WS/Ayarlar:**
   - rj_get_ws_connection_count + ConnectionGuard (RAII doğruluğu)
   - Settings dialog QTabWidget yeniden yapılanması
5. **ISource katmanı (yeni, henüz wire edilmemiş):**
   - i_source.h kontratı
   - desktop_source_logic.h + existing_desktop_source.{h,cpp}

## Bölüm B — Kapsam Dışı (tarayıcılara açıkça söylenecek)

- V8/V9/K-serisinin zaten sertleştirdiği eski bölgeler (capture çekirdeği,
  keyed-mutex, NVENC çekirdeği, metrics plumbing) — yeniden tarama israf.
- pipeline.cpp/run_frame() capture-wiring bölgesi — Faz 3 wiring'i
  yakında değiştirecek, düşük öncelik.
- Bilinen/bilinçli açık kalemler (tarayıcılar bunları "bulgu" diye
  raporlamasın): SRT'de ses yok (MPEG-TS muxer bekliyor), çözünürlük
  kontrolü yok (encode-time downscale bekliyor), gpu_load_pct daima 0
  (YAGNI), termal metrikler stub, resampling yok (48k/2 dışı), MF
  encoder cross-thread shutdown notu, RTMPS yok, kural GUI-düzenleme
  yok, CaptureSubsystem'in kaderi wiring'e ertelendi.

## Bölüm C — Tarama Prompt'u Üret

Üç modele verilecek tek bir ortak prompt yaz (docs/ altına
V10_TARAMA_PROMPT.md olarak). İçermeli:

1. Proje bağlamının kısa özeti (stack, mimari prensipler: FFI'dan
   yalnız veri geçer, tek-thread RTMP invariant'ı, SPSC desenleri,
   SEH/COM disiplinleri).
2. Bölüm A'nın dosya listesi (tam yollarla).
3. Bölüm B'nin kapsam-dışı listesi (yanlış-pozitif önlemek için).
4. Beklenen çıktı formatı: önceki V8/V9 desenine uygun — her bulgu
   için: dosya/satır, iddia edilen sorun, şiddet (kritik/orta/düşük),
   somut kanıt/akıl yürütme. Spekülatif "olabilir" bulguları düşük
   güven etiketiyle ayrılsın.
5. Özel dikkat çağrıları:
   - SPSC ring'in üretici/tüketici thread sınırları (ses).
   - MF/COM nesne yaşam döngüsü (I9/I10 desenlerine uygunluk).
   - Zig FLV mux'ın ABI/bounds güvenliği (J1'in cstr_bounded
     dersinin yeni kod yüzeyinde uygulanıp uygulanmadığı).
   - RAII guard'ların tüm çıkış yollarını kapsaması (ConnectionGuard,
     NullStreakTracker).
   - Qt dosya-sistemi işlemlerinin hata yolları (son üç bug'ın
     bölgesi — QFile/QTemporaryFile semantiği).

## Bölüm D — V10 Plan İskeleti

docs/FABLE5_BUG_PLAN_V10.md iskeletini oluştur (V8/V9 formatıyla):
başlık, kapsam özeti, "bulgular buraya eklenecek" bölümü, tarama
tarihi/model listesi için yer tutucular. Linear'a yeni bir issue
açılması gerektiğini rapora not düş (kullanıcı açacak veya sen
açabiliyorsan aç).

---

## Sabit Kurallar

- Bu talimat kod değiştirmez — yalnızca envanter + prompt + iskelet.
- Dosya listesi git log/git diff ile V9-kapanış commit'inden
  bugüne gerçek değişikliklere dayanmalı — hafızadan yazılmasın.
- Kapsam-dışı listesi eksiksiz olmalı — önceki taramalarda
  yanlış-pozitiflerin (bilinçli kararların "bug" sanılması) zaman
  kaybettirdiği görüldü (J10/J11 çürütmeleri).
- Tamamlanınca kullanıcıya teslim: kapsam paketi + prompt + iskelet.
  Taramanın kendisi kullanıcının üç modele ayrı ayrı yaptıracağı
  sonraki adım.
