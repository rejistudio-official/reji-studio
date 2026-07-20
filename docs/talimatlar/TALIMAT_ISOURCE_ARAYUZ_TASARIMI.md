# TALİMAT: ISource Arayüz Tasarımı — Faz 3'ün Başlangıcı (Faz 0 + Faz 1, İmplementasyon Yok)

**Kaynak:** `docs/ROADMAP.md` Faz 3 (Çoklu Kaynak Mimarisi) — Faz 5
bağımlılık analizinin önerdiği ilk adım. GUI Gözlem Turu'nun bulgusu
(CUT/FADE shader'ı hazır, gerçek kompozisyon yok) bu işin motivasyonu.
**Hedef dosya konumu:** Tamamlanınca `docs/talimatlar/` arşivine
taşınmalı.

---

## Görevin Özü ve Kapsam Uyarısı

**Bu, projenin şimdiye kadarki en büyük mimari girişimi olabilir.**
Diğer büyük turlarda (Ses Ayarları, Donanım Profilleme) izlenen disiplin
burada da geçerli: Faz 0'da kapsamı dürüstçe boyutlandır, büyükse
MVP'ye böl. Bu talimat yalnızca arayüz tasarımına kadar gidiyor —
gerçek çoklu-kaynak implementasyonu (Faz 5'in "Scene composition"
önkoşuluna bağlı) bu talimatın kapsamı dışında, ayrı bir gelecek tur.

**Faz 5 sınırı hatırlatması:** external_memory_bridge.zig'in
instance-level'a taşınması yalnızca gerçek eşzamanlı kompozisyon
(birden fazla kaynağın aynı frame'e render edilmesi) için gerekli.
Bu talimat o noktaya varmıyor — yalnızca arayüzü tasarlıyor ve
mevcut tek-kaynak yolunu (WGC/DXGI) bu arayüze uyarlıyor (adapter),
gerçek fonksiyonel değişiklik yapmadan. Bu sınırı Faz 0/1 boyunca
koru — "madem buradayız, kompozisyonu da yapalım" cazibesine kapılma.

---

## Faz 0 — Mevcut Mimarideki "Tek Kaynak" Varsayımlarının Envanteri

Amaç: ISource arayüzünün neyi soyutlaması gerektiğini bulmak için,
kod tabanında "yalnızca bir aktif kaynak var" varsayımının nerelerde
gömülü olduğunu çıkarmak.

1. **Capture katmanı:** DxgiCapturePipeline/WGC capture kodunun
   (capture_dxgi.cpp, capture_wgc.cpp) API yüzeyini incele — hangi
   fonksiyonlar/veri yapıları zaten "bir capture akışı" soyutlamasına
   yakın, hangileri tekil global duruma (Pipeline::Config gibi) sıkı
   bağlı?
2. **GPU interop köprüsü:** Faz 5 analizinin bulduğu external_memory_bridge.zig
   (b)-kategori state'ini (image_pool, cached_texture_ptr, gl_target_*)
   yeniden gözden geçir — arayüz tasarımı bu state'in gelecekte
   instance-level'a taşınmasını engellemeyecek şekilde kurulmalı
   (bugün taşımıyoruz, ama yarın taşınabilir olmalı).
3. **Encoder:** NvencEncoder'ın (I23, HP1/HP2, J9) tek bir aktif
   encoder session'ı varsaydığını biliyoruz (maxEncodeWidth/Height
   init-time tavan). ISource'un çoklu kaynak senaryosunda encoder'la
   ilişkisi ne olacak — her kaynağın kendi encoder'ı mı (muhtemelen
   hayır, encode edilen tek bir kompozit çıktı), yoksa yalnızca kaynaklar
   kompozit edilip tek encoder'a mı gidiyor? Bu, arayüzün next_frame()
   çıktısının nereye aktığını belirler.
4. **Healing/RuleEngine:** RuleEngine'in metrikleri (frame_drop_pct,
   cpu_load_pct vb.) şu an tekil pipeline düzeyinde. Çoklu kaynak
   geldiğinde healing hâlâ çıktı/encode düzeyinde mi kalacak (muhtemel
   — kaynaklar birleşip tek çıktı olduğundan), yoksa per-kaynak sağlık
   sinyali gerekecek mi? Bu, ISource arayüzünün bir "health"/durum
   alanı taşıyıp taşımayacağını belirler.
5. **Yaşam döngüsü/hata deseni:** RecoveryCoordinator'ın (I10, K2)
   mevcut init/shutdown/hata-kurtarma desenini incele — ISource'un
   start()/stop()/hata sinyali bu deseni yeniden mi kullanmalı, yoksa
   farklı bir yaşam döngüsü mü gerekiyor?
6. **Metadata:** CapabilityDetector/RenderProfile (Donanım Profilleme
   turunda "profil sistemiyle karışmasın" diye ayrıca işaretlenmişti) bir
   kaynağın yeteneklerini (vendor, format, çözünürlük) nasıl temsil
   ediyor — ISource::metadata() bunun genellenmiş hali mi olmalı?

Faz 0 çıktısı: Yukarıdaki altı alanın haritası + her birinin
ISource arayüzünü nasıl şekillendireceğine dair bulgular. Onaya sun.

## Faz 1 — Arayüz Tasarımı (yalnızca kontrat, implementasyon yok)

Faz 0 bulgusuna göre, ama beklenen çıktı biçimi:

1. **ISource C++ arayüzü (saf bildirim, .h dosyası, gövde yok):**
   - next_frame() — pull mu push mu (callback)? Senkron mu async mı?
     Dönüş tipi ne (texture handle + pts + metadata mı)?
   - metadata() — format/çözünürlük/vendor bilgisi.
   - start()/stop()/yaşam döngüsü — RecoveryCoordinator deseniyle
     nasıl ilişkileneceği.
   - Hata/durum sinyali — health kavramı arayüze girecek mi girmeyecek
     mi (Faz 0 madde 4'ün sonucuna göre).
2. **ExistingDesktopSource adapter'ı (tasarım düzeyinde, kod değil):**
   Mevcut WGC/DXGI capture'ı bu arayüze nasıl saracağının taslağı —
   gerçek fonksiyonel davranış değişmemeli, yalnızca mevcut kodun
   üstüne ince bir sarmalayıcı.
3. **Faz 5 ile temas noktası:** Arayüzün, gelecekte external_memory_bridge
   instance-level olduğunda kırılmayacağını (breaking change
   gerektirmeyeceğini) doğrulayan bir kısa gerekçelendirme.
4. **Açıkça kapsam dışı bırakılacaklar listesi:** Gerçek kompozisyon
   (birden fazla ISource'un aynı anda render edilmesi), sahne
   düzenleyici UI, yeni kaynak tipleri (webcam, ekran-bölgesi vb.) —
   hepsi bu talimatın dışında, ayrı gelecek turlar.

Tasarımı onaya sun — implementasyon (gerçek ExistingDesktopSource.cpp
kodu dahil) bu turun kapsamında değil, ayrı bir takip talimatı
olarak yazılacak.

## Sabit Kurallar

- Bu talimatta gerçek kod yazılmıyor — yalnızca .h arayüz taslağı
  (Faz 1'in 1. maddesi) istisna, o da yalnızca bildirim, gövde yok.
- tests/baseline_metrics.txt asla commit edilmez (zaten bu turda
  ilgisiz, ama alışkanlık).
- "Kod incelemesiyle doğrulandı" / "makul çıkarım, belgelenmemiş" ayrımı
  raporda açık olsun — Faz 3'ün kendi tasarım dokümanı yok, çoğu
  bulgunun mevcut koddan çıkarım olacağı önceden biliniyor.
- Kapsam büyürse (örn. Faz 0'da encoder/healing ilişkisi beklenenden
  karmaşık çıkarsa) dur, raporla, MVP'yi daralt — bu, projenin tüm
  büyük turlarında işleyen disiplin.
