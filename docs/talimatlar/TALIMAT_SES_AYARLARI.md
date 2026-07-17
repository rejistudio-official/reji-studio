# TALİMAT: Ses Ayarları — Faz 0 Ön-Koşulu ve Kapsam Belirleme

**Kaynak:** Ayarlar Araştırma Turu, öncelik #3 — "Orta–büyük maliyet,
yüksek değer. WASAPI altyapısı hazır ama `audio_enabled` sabit `false`
+ cihaz seçici yok. OBS paritesi için kritik; kapsamı en geniş kalem."
**Hedef dosya konumu:** Tamamlanınca `docs/talimatlar/` arşivine
taşınmalı.

---

## Görevin Özü ve Kritik Ön-Koşul

Bu talimat, Video Ayarları'ndan (bitrate/FPS) yapısal olarak farklı bir
riskle başlıyor: **`audio_enabled`'ın `false` olması bilinçli bir
tasarım kararı mı, yoksa gerçekten yarım kalmış bir iş mi — bu Faz 0'da
kesinleşmeden hiçbir tasarıma geçilmeyecek.**

Bilinen ipuçları (doğrulanmamış, Faz 0'da teyit edilecek):
- WASAPI capture altyapısı gerçek görünüyor — I18'de `AudioFrameCallback`
  sink deseni bu koda zaten uygulanmıştı (connection-lost/metrics FFI
  wiring'i), yani capture katmanı en azından kısmen olgun.
- `MetricState`'te `audio_bitrate_kbps` diye bir alan **zaten var**
  (GetStats Faz 0'ında görülmüştü) — bu, bir ses encode/mux yolunun en
  azından bir parçasının var olabileceğine işaret ediyor, ama bu alanın
  gerçekten besleniyor mu yoksa ölü mü olduğu bilinmiyor.

**Bu talimat, Video Ayarları'nın aksine, Faz 0'ın sonucuna göre iki çok
farklı büyüklükte işe dönüşebilir** — küçük bir "UI + enable flag'i
bağla" işi ile büyük bir "encoder/mux entegrasyonu eksik, sıfırdan
inşa et" işi arasında. Faz 0'ın görevi bu ayrımı netleştirmek.

---

## Faz 0 — Kapsam Belirleme (kod yazmadan, zorunlu ve kapsamlı)

1. **`audio_enabled`'ı bul** — nerede tanımlı, `false` değerinin
   yanında bir yorum/gerekçe var mı? Git geçmişinde bu satırın ne zaman
   ve neden eklendiğine dair bir ipucu var mı (`git log -p` ile).
2. **WASAPI capture'ın gerçek olgunluğunu değerlendir:**
   - Ses verisi gerçekten yakalanıyor mu (capture loop çalışıyor mu),
     yoksa yalnızca cihaz bildirimleri/hata yolu mu var (I18'in asıl
     dokunduğu kısım bu olabilir, capture'ın kendisi değil)?
   - Yakalanan ses verisi nereye gidiyor — bir buffer'da mı bekliyor,
     yoksa hemen atılıyor mu?
3. **Encode/mux yolunu izle:**
   - Bir AAC/Opus (veya başka) ses encoder'ı wiring'i var mı?
   - `MetricState.audio_bitrate_kbps`'i şu an besleyen bir kod var mı,
     yoksa hep `0`/varsayılan mı kalıyor?
   - SRT/RTMP çıkış konteynerinde bir ses track'i tanımlı mı, yoksa şu
     an yalnızca video mu gönderiliyor?
4. **Cihaz seçimi:** WASAPI'nin belirli bir cihazı (varsayılan değil)
   seçebilme yeteneği kod düzeyinde var mı (cihaz enumerasyonu için
   bir API/fonksiyon), yoksa her zaman sistem varsayılan cihazına mı
   sabitlenmiş?
5. **Gerçek boyut tahmini:** Yukarıdakilere göre üç olası sonuçtan
   birine var:
   - **(a) Küçük:** Capture+encode+mux zaten çalışıyor, yalnızca
     `audio_enabled` bir UI switch'ine bağlanacak + cihaz seçici
     eklenecek. Video Ayarları'na benzer boyutta.
   - **(b) Orta:** Capture var ama encode/mux eksik veya yarım —
     bu kısmın tamamlanması gerekiyor.
   - **(c) Büyük:** Hem capture hem encode/mux gerçek anlamda eksik —
     bu, tek bir talimatın kapsamını aşar, MVP'ye bölünmesi (örn.
     "önce yalnızca capture+seviye göstergesi, encode/mux ayrı tur")
     önerilmeli.

**Faz 0 çıktısı:** `audio_enabled=false`'ın bilinçli/yarım ayrımı +
capture/encode/mux/cihaz-seçimi'nin her birinin gerçek durumu + (a)/(b)/(c)
sınıflandırması + önerilen kapsam (gerekirse MVP'ye bölünmüş). **Bu
talimatın en kritik adımı — acele edilmesin, (c) çıkarsa dürüstçe büyük
olduğunu söyle.** Onaya sun.

## Faz 1 — Tasarım (yalnızca Faz 0 kapsamı netleştikten sonra)

Faz 0 sonucuna göre şekillenecek — bu aşamada önceden tasarım
dayatılmıyor. Olası eksenler (Faz 0'ın bulduğu kapsama göre
uygulanacak/atlanacak):

1. UI: Settings dialog'a "Ses Ayarları" grubu (etkinleştir checkbox +
   cihaz dropdown + varsa seviye göstergesi).
2. Cihaz enumerasyonu: WASAPI `IMMDeviceEnumerator` ile mevcut giriş
   cihazlarını listeleme.
3. Eğer encode/mux eksikse: bu kısmın nasıl ele alınacağı (yeni bir
   talimat mı, yoksa bu talimatın Faz 1'inde mi genişleyecek) — Faz 0
   bulgusuna göre karar.
4. Persistence: `QSettings`, mevcut desenle tutarlı (Video Ayarları'nın
   `video/*` anahtar deseniyle paralel, örn. `audio/*`).

Tasarımı onaya sun, implementasyondan önce.

## Faz 2-3 (Faz 1 onayından sonra tanımlanacak)

Bu talimatın kapsamı Faz 0'da netleşmeden Faz 2-3 planlanmayacak.

## Sabit Kurallar

- CLAUDE.md Bölüm 8b'ye göre dal kararı ver.
- `tests/baseline_metrics.txt` asla commit edilmez.
- "Test edildi" / "kod incelemesiyle doğrulandı" ayrımı raporda açık.
- **Bu talimatın en önemli kuralı:** Faz 0'da kapsamın büyük çıkması
  bir başarısızlık değil, doğru bir tespit. Küçük bir "UI ekle" işi
  bekleyip büyük bir "encoder inşa et" işine rastgele girişme — dur,
  gerçek boyutu raporla, kullanıcıyla birlikte MVP'ye nasıl bölüneceğine
  karar ver.
