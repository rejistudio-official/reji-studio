# TALİMAT: I9 + I10 — COM Yaşam Döngüsü ve SEH Filtreleri

**Kaynak:** `docs/FABLE5_BUG_PLAN_V8.md` (I9, I10), CONTEXT.md bölüm 2/5
**Hedef dosya konumu:** Tamamlanınca `docs/talimatlar/` arşivine taşınmalı.

---

## Görevin Özü

İki madde Windows-özel hata/kaynak yönetimi bölgesinde, muhtemelen aynı
dosya(lar)da yaşıyor — bu yüzden birlikte ele alınıyor. Ortak bir tema
taşımaları dışında bağımsız düzeltmeler; birbirine bağımlı bir tasarım
kararı gerektirmiyorlar (I33/I11'in aksine). Bu yüzden bu talimat daha
hafif: Faz 0 keşfi yine zorunlu, ama Faz 1 "tasarım" değil "yaklaşım
onayı" ölçeğinde olabilir.

- **I9:** `CoUninitialize()` koşulsuz çağrılıyor. COM kuralı: yalnızca
  karşılık gelen `CoInitialize`/`CoInitializeEx` **başarıyla** dönmüşse
  (`S_OK` veya `S_FALSE`) `CoUninitialize` çağrılmalı. Koşulsuz çağrı,
  başlatma başarısız olduğunda (`RPC_E_CHANGED_MODE` gibi) veya COM hiç
  başlatılmamışken çağrıldığında COM'un iç referans sayacını bozar —
  görünürde çalışır ama uzun ömürlü sızıntı veya rastgele kapanışta çökme
  riski taşır.
- **I10:** SEH (`__try`/`__except`) filtreleri erişim ihlali (AV) ve stack
  overflow gibi ciddi hataları yutuyor — muhtemelen genel bir
  `EXCEPTION_EXECUTE_HANDLER` filtresiyle her exception code'u yakalanıp
  sessizce devam ediliyor. Bu, gerçek bellek bozulmasını maskeler; program
  bozuk durumda çalışmaya devam eder, teşhis imkânsızlaşır. Özellikle stack
  overflow'da `__except` bloğunun kendisi çalışacak stack alanı bulamayabilir
  (bilinen Windows SEH tuzağı) — filtre bunu ayırt etmeli.

---

## Faz 0 — Güncel `master`'a Karşı Doğrulama (kod yazmadan, zorunlu)

1. **I9:** `CoInitialize`/`CoInitializeEx` ve `CoUninitialize` çağrılarının
   tüm konumlarını bul (find-references). Her çağrı çifti için: dönüş değeri
   kontrol ediliyor mu, hangi thread'de (STA/MTA), RAII sarmalayıcı var mı
   yoksa çıplak çağrı mı? Kaç farklı yerde tekrarlanan desen var — ortak bir
   RAII tipi (`ComInitializer` gibi) mi gerekiyor yoksa nokta-düzeltme mi
   yeterli?
2. **I10:** Tüm `__try`/`__except` bloklarını bul. Her biri için: filtre
   ifadesi ne (hangi exception code'ları geçiriyor), `GetExceptionCode()`
   kontrolü var mı, hangi exception'lar susturuluyor? Stack overflow
   (`EXCEPTION_STACK_OVERFLOW`) özel olarak ele alınan bir yer var mı?
3. `FABLE5_BUG_PLAN_V8.md`'deki I9/I10 tanımlarını güncel kodla karşılaştır;
   sapma varsa raporla (I2/I3/I8'de olduğu gibi kapsam değişebilir).
4. Her iki madde için de: bu kodun hangi kritik yollarda çalıştığını tespit
   et (startup/shutdown mı, her frame mi, yalnız hata yolunda mı) — düzeltme
   riskini ölçeklendirmek için önemli.

**Faz 0 çıktısı:** Bulunan tüm konumların listesi + her biri için mevcut
davranış + önerilen düzeltme yaklaşımı (nokta-düzeltme mi ortak
sarmalayıcı mı). Onaya sun.

## Faz 1 — Yaklaşım Onayı (implementasyondan önce)

**I9 için:**
- Tercih edilen desen: RAII sarmalayıcı (`ComInitializer` / benzeri; ctor'da
  `CoInitializeEx` çağırır ve sonucu saklar, dtor'da yalnızca başarılıysa
  `CoUninitialize` çağırır). Tekrarlanan çıplak çağrı sayısı 2'den fazlaysa
  bu ortak tipi öner; azsa nokta-düzeltme (`if (SUCCEEDED(hr))` guard) yeterli
  — Faz 0 bulgusuna göre karar ver.
- `RPC_E_CHANGED_MODE` durumunu (bu thread zaten farklı modda başlatılmış)
  ayrı ele al: bu bir hata değil, "zaten başlatılmış" anlamına gelir —
  `CoUninitialize` çağrılmamalı ama program akışı bozulmamalı.

**I10 için:**
- Filtre ifadesi `GetExceptionCode()`'a göre ayrıştırılsın: kritik kodlar
  (`EXCEPTION_ACCESS_VIOLATION`, `EXCEPTION_STACK_OVERFLOW`,
  `EXCEPTION_ILLEGAL_INSTRUCTION` vb.) **yutulmasın** —
  `EXCEPTION_CONTINUE_SEARCH` ile yukarı iletilsin (ya da yapılandırılmış
  bir crash-report/log-then-terminate yoluna gitsin, Faz 0 bulgusuna göre
  öner). Yalnızca gerçekten kurtarılabilir/beklenen exception'lar (varsa)
  yutulmaya devam etsin.
- Stack overflow özel durumu: `_resetstkoflw()` (CRT) kullanılmadan
  `__except` bloğu içinde iş yapmaya çalışmak güvenilmez olabilir — bu kod
  yolunda ne yapılacağını (log dene / doğrudan terminate) Faz 0 bulgusuna
  göre öner ve gerekçelendir.
- **Davranış değişikliği uyarısı:** Bu değişiklik, önceden sessizce devam
  eden bir programın artık **çökebileceği** anlamına gelir. Bu istenen
  sonuç (gizli bozulmayı maskelemek yerine görünür kılmak) ama kullanıcıya
  açıkça bildirilmeli — "hangi senaryolarda artık çöküyor" listesi şart.

## Faz 2 — İmplementasyon (küçük commit'ler)

Önerilen sıra (Faz 0/1'e göre uyarlanabilir):
1. I9 düzeltmesi (RAII sarmalayıcı veya nokta-düzeltmeler, konum başına ya
   da tek commit — Faz 0'daki konum sayısına göre karar ver).
2. I10 düzeltmesi (filtre ayrıştırması, konum başına ya da tek commit).
3. Dokümantasyon: V8 planı güncellemesi, SESSION_NOTES, ilgili skill
   (`build-troubleshoot` veya yeni bulgu varsa uygun skill) notu, talimatı
   arşivle.

## Faz 3 — Test ve Dürüstlük Sınırları

- **I9:** Birim/entegrasyon testiyle doğrulamak zor olabilir (COM thread
  modeli, gerçek OS state'i gerektirir) — mümkünse başarısız
  `CoInitialize` senaryosunu simüle eden bir test yaz; mümkün değilse kod
  incelemesiyle doğrula ve bunu açıkça belirt.
- **I10:** Kasıtlı olarak AV/stack overflow tetikleyen bir test (varsa
  mevcut test altyapısında güvenli şekilde) — riskliyse (stack overflow
  testleri CI'ı çökertebilir) atlanabilir, gerekçesiyle raporla.
- Regresyon: mevcut ctest/rust test paketinde yeni kırık olmadığını doğrula.
- Kullanıcıya görünen davranış değişikliklerini (özellikle I10'un artık
  çökebilme potansiyeli) önden listele.

## Sabit Kurallar (hatırlatma)

- Küçük, mantıksal commit'ler; seri tamamlanıp doğrulanınca topluca push,
  push öncesi onay.
- `tests/baseline_metrics.txt` asla commit edilmez.
- "Test edildi" / "kod incelemesiyle doğrulandı" / "muhakemeyle kabul
  edilebilir" ayrımı her raporda açık.
- Faz 0 bulguları varsayımlarla çelişirse implementasyona geçmeden dur,
  raporla (I2/I3/I8 deseni).
