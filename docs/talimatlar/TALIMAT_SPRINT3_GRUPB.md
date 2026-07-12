# TALİMAT: Sprint 3-4 / Grup B — I21 (Hardcoded Path + Kontrolsüz freopen) + I24 (Sınırsız CStr Okuma)

**Kaynak:** `docs/FABLE5_BUG_PLAN_V8.md` (I21, I24), Sprint 3-4 Faz 0 ön-triyajı (onaylı)
**Hedef dosya konumu:** Tamamlanınca `docs/talimatlar/` arşivine taşınmalı.

---

## Görevin Özü

Grup A (trivial temizlik) tamamlanıp push edildi. Bu talimat Grup B'yi
kapsıyor — Faz 0 ön-triyajı zaten yapıldı ve onaylandı, bu yüzden burada
yeniden triyaj gerekmiyor; doğrudan yaklaşım detaylandırması + implementasyon.

İki madde ortak temada: **girdi/ortam varsayımlarını sıkılaştırma** —
biri dosya sistemi/yol varsayımı, diğeri FFI sınırında güven varsayımı.

- **I21 — Hardcoded path + kontrolsüz `freopen`:** 3 konumda tespit edildi
  (`main.cpp:64`, `ws_server.rs:667`, `ffi.rs:411`) — hardcoded
  `C:\reji-studio\...` yolu + `main.cpp`'de `freopen` dönüş değeri kontrol
  edilmiyor.
- **I24 — Sınırsız `CStr::from_ptr` okuma:** `ffi.rs:580` ve `ffi.rs:977`
  — C tarafından gelen pointer'ın null-terminator'ına kadar sınırsız
  okunuyor; `rj_reload_rules` özellikle caller-path'i (dosya yolu) doğrudan
  bu şekilde alıyor.

---

## Faz 1 — Yaklaşım Detaylandırması (implementasyondan önce onaya sunulacak)

**I21 için:**
1. Hardcoded `C:\reji-studio\...` yolunun 3 konumda **aynı amaca** mı
   hizmet ettiğini doğrula (log dosyası, config, rules.json gibi) — aynı
   amaçsa tek bir path-resolution fonksiyonuna çıkar (DRY); farklı
   amaçlardaysa ayrı ayrı ele al.
2. Hedef: `%LOCALAPPDATA%` (veya proje için daha uygun bir Windows
   standart dizini — `SHGetKnownFolderPath` ile) tabanlı, kullanıcıya
   göre değişen, taşınabilir bir yol. Geriye dönük uyumluluk: eski
   hardcoded yolda veri varsa (mevcut kullanıcı kurulumları) ne olur —
   otomatik taşıma mı, yoksa yeni kurulumlar için mi geçerli? Bunu
   belirt.
3. `freopen` dönüş değeri kontrolü: başarısız olursa (`nullptr` döner)
   ne yapılmalı — sessizce devam mı (log I/O olmadan çalışmaya devam),
   yoksa görünür bir uyarı mı? Loglama alt sistemi bu noktada henüz
   kurulmamış olabileceğinden (bu `freopen` çağrısı muhtemelen log
   dosyasını açıyor), `OutputDebugStringA`/stderr fallback'i düşün.

**I24 için:**
1. `CStr::from_ptr`'ın güvensizliği aslında iki farklı riski kapsıyor:
   (a) null-terminator'a kadar okumanın teorik olarak sınırsız
   olması (bir bug varsa aşırı büyük okuma), (b) `rj_reload_rules`'daki
   path'in **doğrulanmamış** olması (path traversal riski — C++ tarafı
   zaten güvenilir olsa da savunma katmanı olarak). İkisini ayrı ele al.
2. Uzunluk sınırı: makul bir üst sınır belirle (örn. `MAX_PATH` benzeri,
   Windows path'leri için 260 veya uzun-path desteği varsa daha fazla —
   projenin path uzunluğu varsayımını kontrol et) ve bunu aşan
   girdilerde güvenli reddet (panik değil, hata dönüşü).
3. Path kısıtı (`rj_reload_rules` özelinde): gelen path'in beklenen
   dizin altında kalıp kalmadığını (örn. rules.json'un yaşadığı dizin)
   kontrol etmek mantıklı mı, yoksa bu aşırı kısıtlayıcı mı (kullanıcının
   farklı bir rules dosyası seçmesini engelleyebilir)? Projenin mevcut
   kullanım şeklini (find-references ile `rj_reload_rules` çağrı yeri)
   kontrol edip karar ver — I21'deki gibi varsayımı doğrulamadan
   kısıtlama ekleme.

**Ortak:** Bu değişikliklerin ikisi de "sertleştirme" — kullanıcıya
görünen davranış değişikliği normalde olmamalı (geçerli girdilerle her
şey eskisi gibi çalışmalı), yalnızca geçersiz/aşırı girdilerde davranış
değişir (artık güvenli şekilde reddedilir). Bunu Faz 3'te açıkça
doğrula.

## Faz 2 — İmplementasyon (küçük commit'ler)

Önerilen sıra:
1. I21 — path-resolution fonksiyonu (`%LOCALAPPDATA%` bazlı) + 3 çağrı
   yerinin güncellenmesi.
2. I21 — `freopen` dönüş kontrolü + fallback log.
3. I24 — uzunluk-sınırlı `CStr` okuma yardımcı fonksiyonu + kullanım
   yerlerinin güncellenmesi.
4. I24 — `rj_reload_rules` path kısıtı (Faz 1 kararına göre; gerekli
   değilse bu commit atlanır, gerekçesi raporlanır).
5. Dokümantasyon: V8 planı güncellemesi (I21/I24 kapandı), SESSION_NOTES.

## Faz 3 — Test ve Dürüstlük Sınırları

- I21: yeni path-resolution fonksiyonu için birim test (doğru dizin
  döndürüyor mu); `freopen` başarısızlık senaryosu simüle edilebiliyorsa
  test et, değilse kod incelemesiyle doğrula ve belirt.
- I24: uzunluk-sınırlı okuma için birim test (normal path geçer, aşırı
  uzun/eksik-terminator senaryo güvenli reddedilir — gerçek sınırsız bir
  buffer'la test etmek riskliyse sentetik/mock ile sınırı doğrula).
- Regresyon: mevcut ctest/rust test paketinde yeni kırık olmadığını
  doğrula; özellikle `rj_reload_rules`'ın geçerli kullanım senaryosunda
  (mevcut testler varsa) hâlâ çalıştığını teyit et.
- Kullanıcıya görünen davranış değişikliği: yalnızca geçersiz girdi/eski
  path senaryolarında; bunu önden listele (özellikle path taşıma kararı
  varsa).

## Sabit Kurallar (hatırlatma)

- Küçük, mantıksal commit'ler; Grup B tamamlanıp doğrulanınca push öncesi
  onay (Sprint 3-4'ün alt-grup bazlı push disiplini).
- `tests/baseline_metrics.txt` asla commit edilmez.
- "Test edildi" / "kod incelemesiyle doğrulandı" / "muhakemeyle kabul
  edilebilir" ayrımı her raporda açık.
- Faz 1'deki varsayımlar implementasyon sırasında çelişirse dur, raporla.

---

## Not: Bekleyen Teyit (Grup B'yi bloklamıyor, ama unutulmasın)

Grup A'da silinen `DxgiFramePacing` sınıfının DXGI fallback yoluna
(I4/I5/I27/I28/I30/I32'nin ait olduğu, WGC arızalanırsa devreye giren
kod yolu) ait olmadığının find-references ile kanıtlandığını teyit et —
tek cümlelik bir doğrulama yeterli. Bu, Grup B'nin bir parçası değil,
geriye dönük bir denetim — Grup B raporunda ayrıca ele alınabilir.
