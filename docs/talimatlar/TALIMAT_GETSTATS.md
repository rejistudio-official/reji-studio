# TALİMAT: `GetStats` İmplementasyonu + `GetStreamStatus` Stub Alanları

**Kaynak:** Benchmark aracı Faz 0'ında bulunan boşluk — Reji `GetStats`
request'ini hiç desteklemiyor, `GetStreamStatus`'ta üç alan (`outputCongestion`,
`outputBytes`, `outputTotalFrames`) hardcoded stub.
**Hedef dosya konumu:** Tamamlanınca `docs/talimatlar/` arşivine
taşınmalı.

---

## Görevin Özü ve Kapsam Uyarısı

Bu, önceki bazı talimatlardan **daha büyük çıkma ihtimali olan** bir iş
— Faz 0'da net boyutlandırılmalı, gerekirse MVP'ye daraltılmalı (Özellik
#5'te yaptığımız gibi). Amaç: Companion/Stream Deck'in standart
CPU/bellek/disk izleme panellerinin Reji'ye bağlanabilmesi + mevcut
`GetStreamStatus` yanıtının gerçek veri döndürmesi.

**Mimari ilke (unutulmasın):** Bu veri `MetricState`'ten gelmeli —
paralel bir metrik toplama yolu icat edilmeyecek. `MetricState` zaten
merkezi metrik deposu (I14'ten beri); `GetStats`/`GetStreamStatus`
yalnızca bu depodan okuyan yeni bir tüketici olmalı, tıpkı UI durum
barının (I14) ve VendorEvent'lerin (Özellik #2) yaptığı gibi.

---

## Faz 0 — Boyutlandırma ve Veri Kaynağı Haritalama (kod yazmadan)

1. **obs-websocket v5 spec'inin `GetStats` alan listesini** (benchmark
   Faz 0'da zaten çıkarılmıştı — `docs/talimatlar/TALIMAT_BENCHMARK_KARSILASTIRMA.md`'ye
   bak, tekrar aramaya gerek yok) her alan için `MetricState`'te
   karşılığı olup olmadığını kontrol et:
   - `activeFps`, `outputSkippedFrames`, `cpuUsage`, `memoryUsage` —
     muhtemelen mevcut.
   - `availableDiskSpace` — muhtemelen hiç yok, Windows API'den
     (`GetDiskFreeSpaceEx`) yeni bir sorgu gerekebilir.
   - `averageFrameRenderTime`, `renderSkippedFrames`, `renderTotalFrames`
     — OBS'in render-thread'e özgü kavramları; Reji'nin mimarisinde
     (WGC zero-copy) doğrudan karşılığı olmayabilir. **Bunları
     zorlamaya çalışma** — karşılığı yoksa `0` dönmesi (mevcut
     `outputCongestion` stub'ı gibi) kabul edilebilir, spec'i "yalan
     söylememek" için yaklaşık/en-iyi-çaba değer üretmeye çalışmak
     yerine.
   - `webSocketSession*` alanları — bağlantının kendi oturum
     istatistikleri (gelen/giden mesaj sayısı vb.) — bunlar için
     `ws_server.rs`'te zaten bir sayaç var mı kontrol et.
2. **`GetStreamStatus`'taki üç stub alanın** gerçek veri kaynağı olup
   olmadığını kontrol et:
   - `outputBytes` — kümülatif gönderilen byte sayısı; `SrtOutput`/
     `RtmpTransport`'ta böyle bir sayaç var mı?
   - `outputTotalFrames` — kümülatif encode edilen kare sayısı; benzer
     şekilde var mı?
   - `outputCongestion` — OBS'in bu değeri nasıl hesapladığını spec'ten
     çıkar; Reji'nin mimarisinde (SRT/RTMP farklı congestion sinyalleri
     verebilir) bir karşılığı var mı?
3. **Gerçek boyut tahmini:** Yukarıdakilere göre, kaç alan "ucuz"
   (zaten `MetricState`'te veya kolayca eklenebilir bir sayaçla), kaç
   alan "orta" (yeni bir sayaç/izleme gerektirir), kaç alan "zor/
   karşılığı yok" (0 dönmesi kabul edilecek) — üçe ayır.

**Faz 0 çıktısı:** Alan-alan zorluk tablosu + önerilen MVP kapsamı
(muhtemelen: ucuz+orta alanlar implemente edilir, "karşılığı yok"
alanlar açıkça 0/stub olarak bırakılır ve bu bilinçli bir sınır olarak
belgelenir). Onaya sun — bu talimatın en kritik adımı, kapsamı burada
netleşecek.

## Faz 1 — Tasarım (implementasyondan önce onaya sunulacak)

Faz 0 bulgusuna göre şekillenecek, ama genel eksenler:

1. **`GetStats` request handler'ı** — `obs_protocol.rs`'e yeni bir
   request tipi, `MetricState`'ten okuyup obs-websocket'in beklediği
   yanıt şemasına dönüştüren bir fonksiyon.
2. **`GetStreamStatus`'un üç stub alanı** — Faz 0'da bulunan gerçek
   kaynaklara bağlanacak (varsa) ya da bilinçli stub olarak kalacak
   (yoksa, gerekçesiyle).
3. **Yeni sayaçlar gerekiyorsa** (örn. kümülatif byte/kare sayısı),
   bunların nerede tutulacağı (`MetricState`'e mi eklenecek, yoksa
   `OutputSubsystem`/transport katmanında mı) — `MetricState`'in
   merkezi konumu nedeniyle oraya eklenmesi tercih edilir.
4. **Test edilebilirlik:** `GetStats` yanıtının benchmark aracıyla
   (`scripts/benchmark_compare.py`) doğrudan test edilebileceğini
   unutma — bu aracı zaten yazdık, yeni bir test istemcisi icat etme.

Tasarımı onaya sun, implementasyondan önce.

## Faz 2 — İmplementasyon (küçük commit'ler, CLAUDE.md Bölüm 8b'ye uy)

Faz 1 onayına göre uyarlanacak. Muhtemelen birden fazla commit
(handler + her stub alan için ayrı düzeltme + testler) — Bölüm 8b'nin
2-commit eşiğini büyük ihtimalle aşacağından, dal açılması gerekebilir
(kendi değerlendirmeni yap).

## Faz 3 — Test ve Dürüstlük Sınırları

- Birim testleri: `GetStats` handler'ının `MetricState`'ten doğru
  değerleri okuduğunu doğrula.
- Entegrasyon: `scripts/benchmark_compare.py --target reji` ile
  gerçek bir `GetStats` yanıtının alınabildiğini doğrula (bu script
  zaten bunun için hazır).
- Regresyon: mevcut WS/obs-websocket testleri PASS kalmalı.
- Kullanıcıya görünen davranış değişikliği: Companion/Stream Deck'in
  standart izleme panelleri artık gerçek veri gösterebilir — GUI/gerçek
  donanım gözlemi kullanıcıda kalabilir.

## Sabit Kurallar

- Küçük commit'ler; CLAUDE.md Bölüm 8b'ye göre dal kararı ver.
- `tests/baseline_metrics.txt` asla commit edilmez.
- "Test edildi" / "kod incelemesiyle doğrulandı" / "karşılığı yok,
  bilinçli stub" ayrımı her alan için raporda açık olsun — bu talimat
  özellikle "her şeyi doldurmaya zorlama" ilkesini taşıyor.
- Faz 0'da kapsam beklenenden büyük çıkarsa (örn. çoğu alan "orta/zor"
  kategorisindeyse), dur ve MVP'yi daha da daraltmayı öner — tüm alan
  setini tek turda tamamlamak zorunlu değil.
