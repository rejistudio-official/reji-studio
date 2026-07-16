# TALİMAT: Sütun 2 — Reji vs OBS Karşılaştırmalı Ölçüm Aracı

**Kaynak:** `docs/ROADMAP.md`, "Farklılaşma Stratejisi" sütun 2 (hibrit-GPU
laptop niş konumlandırması) — "Reji vs OBS karşılaştırmalı kare-düşürme/
gecikme ölçümü."
**Hedef dosya konumu:** Tamamlanınca `docs/talimatlar/` arşivine
taşınmalı.

---

## Görevin Özü ve Kritik Fark

Bu talimat **Reji Studio'nun kodunu değiştirmiyor.** Amaç, aynı donanımda
Reji ve OBS'i karşılaştırılabilir şekilde ölçmek için küçük, yeniden
kullanılabilir bir **araç** hazırlamak — gerçek ölçüm koşumu (her iki
uygulamayı gerçekten çalıştırmak, aynı oyunu/sahneyi yayınlamak) senin
elinde kalacak, Claude Code bunu otomatikleştiremez.

**Sinerji fırsatı:** OBS de obs-websocket protokolünü destekliyor. I8
sırasında yazılan `test_obs_client.py`/`test_obs_websocket_js.js`
script'leri, teorik olarak **hem Reji'ye hem gerçek OBS'e** bağlanıp
`GetStats`/`GetStreamStatus` üzerinden metrik çekebilir — yeni bir ölçüm
sistemi icat etmeden, var olan aracı genişleterek.

---

## Faz 0 — Araştırma (kod yazmadan)

1. **Reji'nin `GetStreamStatus`/`GetStats` yanıtının** (Faz 1'de,
   obs-websocket protokol uyumluluğunda inşa edilmişti) tam alan setini
   çıkar — fps, bitrate, dropped frames hangi isimlerle dönüyor?
2. **obs-websocket v5 spec'inin gerçek OBS'te `GetStats`/`GetStreamStatus`
   için döndürdüğü alan setini** araştır (`web_search` ile resmi
   obs-websocket dokümantasyonuna bak) — Reji'nin alan isimleri/birimleri
   gerçek OBS ile bire bir eşleşiyor mu, yoksa dönüştürme gerekiyor mu?
3. Mevcut `test_obs_client.py`/`test_obs_websocket_js.js`'in bugünkü
   halini incele — bunlar tek-seferlik bağlantı testleri mi, yoksa
   zaman içinde örnekleme yapabilecek bir yapıda mı?

**Faz 0 çıktısı:** Alan eşleştirme tablosu (Reji alan adı ↔ OBS alan
adı ↔ ortak/normalize edilmiş isim) + mevcut script'lerin genişletilebilirlik
durumu. Onaya sun.

## Faz 1 — Tasarım (implementasyondan önce onaya sunulacak)

1. **Araç formu:** Tek bir Python script (`scripts/benchmark_compare.py`
   gibi) — komut satırından hedef (Reji portu / OBS portu), süre
   (örn. 60sn), örnekleme aralığı (örn. 1sn) parametreleriyle çalışır.
   Her iki uygulamaya **ayrı ayrı, sırayla** bağlanıp aynı formatta CSV
   üretir (`fps`, `dropped_frames`, `bitrate`, `timestamp`).
2. **Kapsam sınırı (net):** Script iki uygulamayı **otomatik başlatmaz/
   yönetmez** — kullanıcı her ikisini elle açar, aynı sahneyi/oyunu
   yayınlar, script yalnızca zaten çalışan bir WS sunucusuna bağlanıp
   örnekler. Otomasyon (launch/orchestration) bu turun kapsamı dışı —
   YAGNI, gerçek karşılaştırmanın adil olması için zaten kullanıcının
   iki ortamı da aynı şekilde hazırlaması gerekiyor.
3. **Çıktı formatı:** CSV + basit bir özet (ortalama/max dropped frames,
   ortalama fps) konsola yazdırılsın — grafik/görselleştirme bu turun
   kapsamı dışı (istenirse ayrı, küçük bir eklenti olabilir).
4. **Auth uyumluluğu:** Reji'nin WS parolası ayarlıysa script bunu
   parametre olarak alabilmeli (I8'in auth modeliyle uyumlu).

Tasarımı onaya sun, implementasyondan önce.

## Faz 2 — İmplementasyon

1. `scripts/benchmark_compare.py` — bağlantı + örnekleme + CSV çıktısı.
2. Kısa bir `README` notu (script'in nasıl kullanılacağı — iki
   uygulamayı nasıl hazırlaman gerektiği, hangi parametrelerle
   çalıştırılacağı).

## Faz 3 — Test ve Dürüstlük Sınırları

- Script'in Reji'ye gerçekten bağlanıp örnek topladığını test et
  (test ortamında çalışan bir Reji instance'ına karşı).
- Gerçek OBS'e bağlanma testi **kullanıcıda kalacak** — Claude Code'un
  ortamında muhtemelen OBS kurulu değil.
- Bu bir kod değişikliği değil, ayrı bir araç — mevcut `PipelineCharacterization`
  vb. regresyon testleri etkilenmemeli (script `src/` dışında yaşıyor).

## Faz 4 — Sonrası (bu talimatın kapsamı dışı, yalnızca hatırlatma)

Araç hazır olduğunda, **gerçek ölçüm koşumu ve sonuçların belgelenmesi
(README/marketing materyali)** senin elinde — bu, ayrı bir "kullanıcıda"
adımı olacak, Todoist'e o zaman eklenecek.

## Sabit Kurallar

- Küçük commit(ler); tamamlanınca push öncesi onay.
- "Test edildi" / "kod incelemesiyle doğrulandı" ayrımı raporda açık.
- Faz 0'da alan eşleştirmesi beklenenden karmaşık çıkarsa (örn. OBS'in
  bazı alanları hiç karşılığı yoksa) dur, raporla — normalize edilmiş
  ortak alan setini küçük tutmak tercih edilir, tam eşleştirme
  zorlanmasın.
