# TALİMAT: V10 Sprint 2 — L8-L12 + L21-L23

**Kaynak:** docs/FABLE5_BUG_PLAN_V10.md, Sprint 1'in (L1-L6 + dört
acil müdahale, 543c1b3'te merge edildi) ardından sıradaki madde grubu.
**Hedef dosya konumu:** Tamamlanınca ilgili talimat dosyaları
docs/talimatlar/ arşivine taşınmalı.

---

## Görevin Özü ve Sprint 1'den Miras Kalan Ders

Plan dosyasının kendi notu: "Dört ardışık merge-öncesi bulgunun ortak
paydası, mutlu-yol testlerinin 'uygulama açık ama yayın yok' başlangıç
durumunu hiç kapsamaması." Bu sprint'te her düzeltme için hem
boşta/idle hem aktif-yayın senaryosunu ayrı ayrı test et — Sprint
1'in en pahalı dersi buydu.

Bu sprint iki gruptan oluşuyor — sırayla ele al:

## Grup A — L21-L23 (öncelik, birbirine bağlı, Sprint 1'in canlı
## testinde bulundu)

Bu üçü aynı bölgenin (healing'in predictive/kural-motoru ikili yapısı)
farklı yüzleri — birlikte ele almak, aynı koda üç ayrı turda dokunmayı
önler.

1. L21 — Predictive katman SRT bağlantı-yokluğunu frame-drop
   sanıyor (healing.rs:657-664). Bağlantı-kaybı durumunda
   ReduceBitrate yerine zaten var olan notify_connection_lost
   fallback yoluna yönlendirilmeli — tıkanıklık tedavisi yokluk
   sorununu çözmüyor.
2. L22 — frame_drop_pct ölü metrik. record_frame/
   record_frame_drop hiç çağrılmıyor. Faz 0 sorusu: dirilt (gerçek
   çağrı noktaları ekle) mi, yoksa kural setinden tamamen çıkar mı
   (şu an frame_drop_recovery koşulsuz true veriyor — bu ikisi
   birbiriyle bağlı, L21 düzeltilince bu kuralın anlamı değişebilir).
3. L23 — SRT bağlantı durum geçişlerini run.log'a yaz.
   Gözlemlenebilirlik — Sprint 1'in "kesinleşmeyen nokta"sını kalıcı
   kapatır, gelecekte benzer teşhisleri hızlandırır.

Faz 0: Üçünün gerçek bağımlılık sırasını netleştir (muhtemelen
L23 önce — gözlemlenebilirlik olmadan L21'in düzeltmesini canlı
doğrulamak zor olur). L22'nin "dirilt mi kaldır mı" sorusuna gerekçeli
bir öneri sun, onaya sun.

## Grup B — L8-L12 (orta öncelik, bağımsız maddeler)

4. L8 — Zig ABI üst-sınır eksikliği. rj_rtmp_send_audio/
   set_audio_config/rj_rtmp_send'e boyut tavanı ekle (J1
   cstr_bounded deseni). writeFlvTag'in 0xFFFFFF sessiz
   kırpmasını reddetmeye çevir.
5. L9 — MFT CAN_PROVIDE_SAMPLES yanlış yorumu + hata-yolu pSample
   sızıntısı. Faz 0'da gerçek MFT davranışını (caller-buffer
   gereksinimi) doğrula, düzelt.
6. L10 — Kanal-uyumsuzluğunda sessiz bozuk encode. Format
   uyuşmazlığında ses yolunu güvenli kapat.
7. L11 — step_kbps ölü parametre (Donanım Profilleme). Faz 0
   sorusu: bilinçli tasarım mı (yüzde-tabanlı tercih), yoksa unutulmuş
   bağlantı mı? Bilinçliyse profillerden step_kbps kaldırılmalı
   (yanıltıcı alan bırakma), değilse param1 kullanılmalı.
8. L12 — A/V pts epoch doğrulaması. Önce frame_pacer.h/.cpp
   oku — ses (WASAPI QPC-mutlak) ve video (FramePacer::pts_us)
   pts tabanlarının aynı olup olmadığını kesinleştir. Aynıysa çürüt
   ve plana işle, farklıysa düzelt.

---

## Sıra ve Onay Akışı

Önce Grup A (L21→L23→L22, ya da Faz 0'ın bulacağı gerçek sıra),
sonra Grup B — madde madde onaya sun, önceki sprint'teki gibi. Her
düzeltme kendi Faz 0'ından geçsin, rapor iddiası kanıt değildir.

## Sabit Kurallar

- CLAUDE.md Bölüm 8b'ye göre dal kararı — Grup A birbirine bağlı,
  muhtemelen tek dalda; Grup B'nin bağımsız maddeleri ayrı
  değerlendirilebilir.
- Her düzeltme için hem boşta hem aktif-yayın senaryosu ayrı test
  edilsin (bu sprint'in özel kuralı, Sprint 1 dersinden).
- tests/baseline_metrics.txt asla commit edilmez (Sprint 1'deki
  bilinçli istisna — davranış kasıtlı değişti kanıtı — burada
  geçerli değil, yalnızca o özel durum için yapılmıştı).
- "Test edildi" / "kod incelemesiyle doğrulandı" ayrımı raporda açık.
- Merge öncesi kullanıcının canlı GUI doğrulaması istenecek — Sprint
  1'in kanıtladığı gibi bu adım atlanabilir değil.
