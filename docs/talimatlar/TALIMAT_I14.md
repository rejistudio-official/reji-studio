# TALİMAT: I14 — `rj_metrics_poll` İmplementasyonu

**Kaynak:** `docs/FABLE5_BUG_PLAN_V8.md` (I14), CONTEXT.md bölüm 2/5
**Hedef dosya konumu:** Tamamlanınca `docs/talimatlar/` arşivine taşınmalı.

---

## Görevin Özü

`rj_metrics_poll` şu an implemente değil. Bu, Sprint 2'nin son açık maddesi —
tamamlanınca V8 planının Sprint 1-2'si tamamen kapanmış olacak.

**Bilinmesi gereken bağlam (önceki turlardan):** Bu proje boyunca "implemente
değil" ibaresi birkaç kez göründüğünden daha derin çıktı (I19: sinyal hiç
iletilmiyordu; I33: onay tamamen hayaliydi). Bu yüzden Faz 0'da yalnız
"fonksiyon boş mu" diye bakmak yetmez — **kim çağırıyor, çağırmıyorsa neden,
ve çağrılsa bile sonucu gerçekten bir yere ulaşıyor mu** sorularının hepsi
sorulmalı.

**I10'dan taşınan bir ilke burada da geçerli:** Metrik toplama, `MetricsSubsystem`
üzerinden zaten periyodik bir FFI push yoluna sahip (I10'da `MetricsPush` bir
SEH-leaf site'ı olarak zaten haritalanmıştı). `rj_metrics_poll`'un bu mevcut
push mekanizmasıyla ilişkisi ne — onun yerini mi alacak, ona ek mi, yoksa
tamamen ayrı bir tüketici (örn. UI'ın kendi isteğiyle anlık metrik çekmesi)
mi? Bu netleşmeden implementasyona geçilmemeli.

---

## Faz 0 — Güncel `master`'a Karşı Doğrulama (kod yazmadan, zorunlu)

1. `rj_metrics_poll`'un mevcut imzasını, dönüş tipini ve gövdesini incele —
   tam olarak ne eksik (stub mı, yarım mı, yoksa hiç mi yok)?
2. find-references: bu fonksiyonu **çağıran** her yer (C++ tarafı). Hiç
   çağrılmıyorsa (I19 deseni), bunu açıkça raporla — implementasyon tek
   başına yeterli olmayabilir, wiring de gerekebilir.
3. Mevcut metrik akışını haritala: `MetricsSubsystem` neyi, ne sıklıkta,
   hangi FFI fonksiyonuyla Rust'a **push** ediyor (I10'daki `MetricsPush`
   site'ından yola çık)? `rj_metrics_poll` bu push modelinden farklı bir
   **pull** modeli mi öngörüyor? İkisi çakışıyor mu, tamamlıyor mu?
4. `rj_metrics_poll`'un dönmesi beklenen veri neyin karşılığı — FFI
   struct'ı var mı (varsa `ffi_bridge.h`'de), yoksa tasarlanması mı
   gerekiyor?
5. Bu fonksiyonun hangi tüketiciye hizmet etmesi planlanmıştı — UI'ın
   anlık metrik göstergesi mi (örn. bir dashboard/overlay), yoksa
   `obs-websocket` `GetStreamStatus` gibi bir protokol yanıtı için mi
   (Faz 1'de metrik alanları zaten var, oradan mı besleniyor)? Kod
   incelemesiyle veya çağrı bekleyen bir UI elemanı bulunarak teyit et.
6. `FABLE5_BUG_PLAN_V8.md`'deki I14 tanımını güncel kodla karşılaştır;
   sapma varsa raporla (I2/I3/I8/I9/I10 deseni).

**Faz 0 çıktısı:** Mevcut durum haritası (push modeli + poll'un konumu +
çağıran/çağırmayan taraf) + eski varsayımlardan sapmalar. Onaya sun.

## Faz 1 — Yaklaşım Onayı (implementasyondan önce)

Faz 0 bulgusuna göre şekillenecek, ama muhtemel eksenler:

- **Push vs pull kararı:** Mevcut periyodik push yeterliyse `rj_metrics_poll`
  belki de gereksiz bir ikinci yol — bu ihtimal de açıkça değerlendirilmeli
  (silme/no-op önerisi dahi Faz 0 bulgusuna göre masada olabilir). Gerçekten
  gerekliyse (örn. UI'ın kendi isteğiyle anlık veri çekmesi gerekiyor —
  push aralığını beklemek istemiyor), pull'un veri kaynağı push'un
  kullandığı **aynı** iç state olmalı (iki ayrı metrik toplama yolu
  icat etme).
- **Thread-safety:** Poll, push'un yazdığı state'e farklı bir thread'den
  (örn. UI thread'i) erişecekse, senkronizasyon nasıl sağlanacak
  (atomic snapshot, mutex, vs.) — mevcut push yolunun zaten kullandığı
  mekanizmayı taklit et, yeni bir tane icat etme.
- **FFI yüzeyi:** Struct tasarımı (yoksa), `ffi-safety-review` prosedürü
  (repr(C), static_assert, cbindgen).
- **C++ tarafı wiring:** Çağıran yer yoksa nereye eklenmeli (I19 dersi:
  implementasyon + wiring birlikte, ayrı ayrı değil).

## Faz 2 — İmplementasyon (küçük commit'ler)

Faz 0/1 bulgusuna göre uyarlanacak, örnek iskelet:
1. Rust tarafı: `rj_metrics_poll` gerçek implementasyonu (veya Faz 1
   kararına göre kaldırma/no-op gerekçesi).
2. FFI struct (gerekiyorsa) + roundtrip testi.
3. C++ tarafı wiring (gerekiyorsa) + startup/çağrı noktası.
4. Dokümantasyon: V8 planı I14 durumu, SESSION_NOTES, ilgili skill notu,
   talimatı arşivle. **Bu madde kapandığında Sprint 2'nin tamamının
   kapandığını CONTEXT.md'ye ve dış senkron kaynaklarına (Notion/Todoist/
   Linear) yansıt.**

## Faz 3 — Test ve Dürüstlük Sınırları

- Rust birim testleri: poll'un döndürdüğü verinin push'un yazdığı state'le
  tutarlı olduğunu doğrula.
- FFI roundtrip (gerekiyorsa).
- Regresyon: mevcut ctest/rust test paketinde yeni kırık olmadığını doğrula.
- Kullanıcıya görünen davranış değişikliği varsa (örn. UI'da yeni bir
  metrik görünümü açılıyorsa) önden listele; yoksa "iç API tamamlama,
  görünür davranış değişikliği yok" olarak belirt.

## Sabit Kurallar (hatırlatma)

- Küçük, mantıksal commit'ler; seri tamamlanıp doğrulanınca topluca push,
  push öncesi onay.
- `tests/baseline_metrics.txt` asla commit edilmez.
- "Test edildi" / "kod incelemesiyle doğrulandı" / "muhakemeyle kabul
  edilebilir" ayrımı her raporda açık.
- Faz 0 bulguları varsayımlarla çelişirse implementasyona geçmeden dur,
  raporla (I2/I3/I8/I9/I10 deseni) — bu maddede özellikle "implementasyon
  gerçekten gerekli mi" sorusunun kendisi de bu disipline dahil.
