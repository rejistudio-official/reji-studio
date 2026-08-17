# TALİMAT: VFR/CFR — Faz 1 Tasarım (Kare Tekrarı ile CFR Üretimi)

**Kaynak:** `BULGULAR_VFR_CFR_FAZ0.md` — sorun gerçek ve ciddi olarak
sonuçlandı. Seçenek 1 (son kareyi kopyala + null tick'te yeniden
encode et) önerildi; 2, 3, 4 gerekçeleriyle elendi.

**Bu talimat tasarım üretir — kod yazma, Faz 2'ye kadar.**

---

## Çözülecek Üç Sorun (Faz 0'dan)

1. **GOP zaman ekseninde kayıyor** — kare-sayısı bazlı (120), teslim
   fps'i düşünce keyframe aralığı 24 sn'ye hatta sonsuza uzuyor.
   Platform sınırı: YouTube 4 sn, IVS/Twitch 5 sn.
2. **Statik ekranda ses de duruyor** — `audio_bridge_.drain()` yalnız
   video `on_packet` içinden çağrılıyor; video karesi yoksa AAC de
   gitmiyor, RTMP'ye hiç veri akmıyor. Bir reji ürününde BRB/slayt
   ekranı çekirdek senaryo.
3. **VFR akış** — platformlar resmen yasaklamıyor ama tüm gereksinimleri
   (sabit keyframe kadansı, CBR, fps beyanı) CFR varsayıyor.

---

## Faz 1'de Kararlaştırılacaklar

### K1 — Texture kopyalama stratejisi

**Kısıt (Faz 0/A2):** WGC, null döndürmeden **önce** önceki texture'ı
bırakıyor (`capture_wgc.cpp:171-176`, `last_tex_.Reset()`). Yani null
anında son kare elimizde değil — kopya **geçerli karede** alınmalı.

Karara bağlanacak:
- Kopya nerede tutulacak? (pipeline'a ait bir `ComPtr<ID3D11Texture2D>`
  cache, boyut/format değişiminde yeniden yaratılır)
- `CopyResource` her geçerli karede mi çalışacak, yoksa yalnız
  "son karede kopya yok" durumunda mı? Her karede kopyalamak ~0,5 ms
  sabit maliyet demek — `[SendDiag]` bütçesine etkisi ölçülmeli
  (şu an `tot` 6,7-8,8 ms / 16,7 ms bütçe, yer var ama boşuna
  harcanmasın).
- DXGI yolunda durum ne? (`ExistingDesktopSource` iki backend'i de
  sarıyor — DXGI'de texture ömrü farklı olabilir, kontrol et.)

### K2 — Tekrar tetikleme mantığı

- Null tick'te **her zaman** mı tekrar gönderilecek, yoksa bir
  eşik/kadans mı olacak? (örn. "en fazla N ardışık tekrar" veya
  "yalnız GOP süresi dolmak üzereyse")
- Öneri değerlendirilsin: basit tutmak (her null tick'te tekrar)
  vs akıllı olmak (yalnız gerektiğinde). Basit olan öngörülebilir ve
  test edilebilir; akıllı olan bant genişliği tasarrufu sağlar ama yeni
  bir davranış yüzeyi açar. **YAGNI tarafında kal, gerekçelendir.**
- Capture-loss durumu (`SourceState::NeedsReinit`) ile normal null
  ayrımı: kaynak gerçekten kaybolduğunda da tekrar göndermeli miyiz?
  (Muhtemelen evet — yayın kopmasın — ama bilinçli karar olsun.)

### K3 — GOP'u zaman bazlı yapmak

Kare tekrarı GOP sorununu **dolaylı** çözüyor (teslim 60 fps'e
oturunca 120 kare ≈ 2 sn). Ama bu, tekrar mekanizmasının kusursuz
çalışmasına bağlı bir garanti.

Karara bağlanacak: ek olarak **zaman bazlı IDR** de eklensin mi
(örn. "son IDR'dan 2 sn geçtiyse `force_idr`")? Bu, savunma katmanı
olur — tekrar mekanizması bir sebeple duraksarsa bile keyframe kadansı
korunur. Maliyeti düşük görünüyor (`force_idr` mekanizması zaten var,
`pipeline.cpp:609`'da `start_stream`'de kullanılıyor).

### K4 — Ses drain kadansı

Faz 0, kare tekrarının A5'i (ses durması) **kendiliğinden** çözeceğini
söylüyor — her tick'te encode → `on_packet` → drain.

Doğrula: bu gerçekten yeterli mi, yoksa ses drain'ini video
callback'inden **bağımsız** hale getirmek daha sağlam bir çözüm mü?
(İkincisi daha büyük bir değişiklik; yalnız birincisi yetmiyorsa.)

### K5 — Ölçülebilirlik

`[SendDiag]` çıktısına ne eklenecek? En az:
- `dup=` (bu saniyede kaç tekrar kare gönderildi)
- `copy=` (CopyResource süresi ort/max)

Bu, Faz 2'nin doğrulamasında ve gelecekteki regresyonlarda gerekli
olacak — bugün `wire_fps` eklemenin değeri gibi.

---

## Faz 1 Çıktısı

1. Beş kararın (K1-K5) gerekçeli cevapları.
2. Dokunulacak dosyaların listesi + tahmini satır sayısı.
3. Faz 2 için commit sırası önerisi (her commit derlenip çalışmalı —
   ISource wiring turundaki gibi additive-önce).
4. Test planı: hangi birim testleri, hangi entegrasyon testi, ve
   kullanıcıda kalacak canlı doğrulama.
5. **Risk notu:** Bu değişiklik `run_frame` hot-path'ine dokunuyor —
   bugün preview darboğazını orada bulduk. Regresyon riski ve nasıl
   önleneceği açıkça yazılsın.

**Tasarımı onaya sun. Faz 2 (implementasyon) yalnız onaydan sonra.**

---

## Sabit Kurallar

- Bu turda kod yazma.
- `[SendDiag]` bütçesi (`tot < 16.7ms`) tasarımın kabul kriteri —
  kopyalama + ek encode bunu aşmamalı.
- Bitrate maliyeti Faz 0'da **ölçülmedi** (skip-MB P-frame varsayımı,
  birkaç yüz bayt/kare) — Faz 2'de ölçülecek, tasarımda bunu doğrulama
  adımı olarak planla.
- Emin olunmayan noktalar "doğrulanmadı" diye işaretlensin.
