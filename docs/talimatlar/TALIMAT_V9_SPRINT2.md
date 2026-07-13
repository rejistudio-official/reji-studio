# TALİMAT: V9 Sprint 2 — J5, J6, J7, J8

**Kaynak:** `docs/FABLE5_BUG_PLAN_V9.md` (J5-J8), üç bağımsız model
incelemesinin (Fable 5, Opus 4.8, GLM 5.2) sentezi.
**Hedef dosya konumu:** Tamamlanınca `docs/talimatlar/` arşivine taşınmalı.

---

## Görevin Özü

Sprint 1 (J1-J4) tamamen kapandı — hiçbir madde hayalet çıkmadı, V8'in
kapattığı hiçbir kapı yeniden açık bulunmadı. Sprint 2, Sprint 1'den
**yapısal olarak farklı**: dört madde de V8'in hiç bakmadığı taze alan
(kör nokta/takip değil, saf [YENİ]). Konsensüs seviyeleri Sprint 1'e
yakın (J5/J6 3/3, J7/J8 2/3) ama hiçbiri geçmiş bir kararı tersine
çevirmiyor — bu yüzden Faz 1 onay eşiği J4'teki kadar hassas değil, yine
de her madde kendi Faz 0'ından geçecek (V9'un statik-tarama kökenine
şüphecilik istisnasız devam ediyor).

Sıra önerisi: **J6 → J5 → J7 → J8** (mekanik/düşük belirsizlikten,
yorum gerektiren/belirsizliği yüksek olana).

---

## J6 — AMD fallback spin-wait timeout'suz

**V9 kaynağı:** Fable5 3.6, Opus 3.4, GLM 2.3 (3/3)

### Faz 0 (zorunlu)
1. `src/pipeline/capture/capture_dxgi.cpp`'de `amd_copy_fence_` spin'ini
   bul (`YieldProcessor()` ile `GetData(...DONOTFLUSH)` bekleme döngüsü).
2. Döngünün gerçekten sınırsız olduğunu teyit et — bir üst sınır/timeout
   zaten var mı (varsa V9 bulgusu bayat, raporla).
3. Bu spin'in hangi koşulda tetiklendiğini belirle (yalnız AMD fallback
   yolunda mı, WGC aktifken hiç mi çalışmıyor — I23/WGC-DXGI bağlamıyla
   ilişkisini kur, gerekirse `vulkan-interop-debug` skill'ine bak).
4. TDR/hang riskinin gerçekliğini değerlendir: GPU çökerse (TDR) bu spin'in
   gerçekten sınırsız süre frame thread'i dondurup dondurmadığını kod
   incelemesiyle teyit et.

### Faz 1 (yaklaşım — muhtemelen basit, kısa onay yeterli)
Üç inceleyici de aynı yönde: sınırlı spin sayısı sonrası `Sleep(0)`/
`SwitchToThread` eskalasyonu veya event-query + bounded sleep. Faz 0
bulgusuna göre somut bir sınır öner (örn. N spin sonrası fallback).
Davranış değişikliği: yalnızca gerçek bir GPU hang/TDR senaryosunda fark
yaratır (normal koşulda spin zaten kısa sürede tamamlanıyor olmalı) —
bunu Faz 3'te doğrula.

### Faz 2-3
Küçük, tek commit. Regresyon: bu yolun normal (AMD, GPU sağlıklıyken)
davranışının değişmediğini teyit et — yalnızca patolojik durumda (spin
tamamlanmıyorsa) fark ortaya çıkmalı.

---

## J5 — `action_thread_main` sabit 100ms poll

**V9 kaynağı:** Fable5 4.3, Opus 4.2, GLM 4.2 (3/3)

### Faz 0 (zorunlu)
1. `src/pipeline/command_router.cpp`, `action_thread_main()`'i bul.
2. Dequeue sonrası `Sleep(100)`'ün kuyruk durumundan bağımsız her zaman
   çalıştığını teyit et (kuyrukta başka aksiyon olsa bile uyuyor mu?).
3. Bu gecikmenin healing aksiyonlarını gerçekten geciktirdiğini (I33+I11
   ile kurulan action-queue mimarisiyle etkileşimini) değerlendir — J5,
   I33'ün kurduğu iki-kuyruk mimarisinin (aktüatör kuyruğu) tüketici
   tarafını mı etkiliyor, yoksa farklı bir kuyruk mu? Netleştir.

### Faz 1 (yaklaşım — iki seçenek var, onay gerekli)
V9 iki seçenek sunuyor:
- **(a) İç döngüde kuyruğu boşalt, sonra uyu** — küçük, düşük riskli
  değişiklik, mevcut poll modelini korur.
- **(b) Event/condvar tabanlı sinyal** (Opus'un önerisi) — daha temiz
  ama daha büyük değişiklik, Rust tarafından sinyal gerektirebilir.
Faz 0 bulgusuna göre öner; **(a) muhtemelen yeterli ve bu sprint'in
risk profiline daha uygun** — (b) gerekiyorsa kapsamı büyüteceğinden
ayrı değerlendirilebilir, kararı Faz 1'de gerekçeyle sun.

### Faz 2-3
(a) seçilirse tek commit. Regresyon: `action_thread_main`'in davranışını
test eden mevcut bir test var mı kontrol et, yoksa küçük bir birim/
entegrasyon testi ekle (kuyrukta N aksiyon varken hepsinin tek 100ms
periyodunda tüketildiğini doğrulayan).

---

## J7 — Keyed-mutex key sabitleri paylaşımlı header'da değil

**V9 kaynağı:** Fable5 3.3, Opus 3.3 (2/3) — **aktif bug değil, bakım riski**

### Faz 0 (zorunlu — özellikle "aktif bug yok" iddiasını doğrula)
1. `copy_optimizer.cpp` (`km_acquire_key_=1`, `km_release_key_=0`) ve
   `capture_dxgi.cpp` (`AcquireSync(0)`/`ReleaseSync(1)`) key değerlerini
   bul.
2. **GLM'in bu protokolü satır satır doğrulayıp doğru bulduğunu** V9
   belgesinden hatırla — kendi başına yeniden doğrula, GLM'in bulgusuna
   körü körüne güvenme.
3. Bu, gerçek bir bug değil — "değerler şu an tutarlı ama paylaşımlı
   sabit yok, gelecekte biri bir tarafı değiştirirse sessizce deadlock'a
   dönüşebilir" iddiasını değerlendir. Bu iddianın makul olup olmadığına
   karar ver (kod her iki dosyada da ne sıklıkla değişiyor, bu bir gerçek
   risk mi yoksa teorik mi).

### Faz 1 (yaklaşım — düşük risk, hızlı onay yeterli)
Key sabitlerini paylaşımlı bir header'a (`reji_constants.h` gibi, proje
konvansiyonuna uygun bir isim/konum öner) çıkar, her iki taraf oradan
referans alsın. Saf refactor — davranış değişikliği olmamalı.

### Faz 2-3
Tek commit. Regresyon: `GpuResourcePitch`/`PipelineCharacterization`
testleri PASS kalmalı, değerler birebir aynı kalmalı (yalnız konumları
değişiyor).

---

## J8 — `MetricsCollector::poll()` frame thread'de PDH sorgusu

**V9 kaynağı:** Fable5 4.4, Opus 4.3 (2/3) — **I14 ile karıştırılmaması
gereken, dikkatli Faz 0 gerektiren madde**

### Faz 0 (zorunlu — bu maddede özellikle titiz ol)
1. **Önce net ayrım kur:** I14, `rj_metrics_poll` FFI fonksiyonunu
   (`MetricState`'ten UI'ın atomik okuması, AGENTS.md'nin blokla­yan-sorgu
   yasağına zaten uygun tasarlandı) implemente etmişti. J8 **farklı bir
   bileşen** — `src/pipeline/metrics_collector.cpp`'deki `MetricsCollector::poll()`,
   `PdhCollectQueryData` çağıran C++ fonksiyonu. İkisini karıştırma;
   raporunda bu ayrımı açıkça belirt.
2. `MetricsCollector::poll()`'un gerçekten `pipeline.cpp::run_frame()`'den
   (frame thread) çağrıldığını teyit et.
3. **`AGENTS.md`'nin tam metnini oku** (muhtemelen `AGENTS.md:114`
   civarı, I14 talimatında referans verilmişti) — kısıt tam olarak neyi
   yasaklıyor: "hot-path'te asla bloklayan sorgu" mı, yoksa "throttle'lı
   olsa bile hiç çalışmasın" mı? Bu ayrım, J8'in gerçek bir ihlal mi yoksa
   kabul edilebilir bir istisna mı olduğunu belirleyecek.
4. 1Hz throttle'ın gerçekte var olduğunu ve nasıl çalıştığını doğrula
   (Opus'un raporu bunu "throttle'lı olsa da hâlâ frame thread'de
   çalışıyor, periyodik jitter yaratıyor" diye değerlendiriyor).
5. Gerçek etkiyi ölç/değerlendir: throttle'lı 1Hz PDH sorgusu, 60fps'lik
   bir frame thread'de ne kadar jitter yaratıyor — bu ölçülebilirse
   (mevcut `FrameProfiler` ile) ölç, ölçülemezse kod incelemesiyle
   makul bir tahmin ver.

### Faz 1 (yaklaşım — onay gerekli, I14 ile tutarlı tasarım)
Eğer Faz 0 gerçek bir ihlal/etki doğrularsa: `MetricsCollector::poll()`'u
ayrı bir 1Hz arka plan thread'ine taşı; `run_frame()` yalnızca atomik
snapshot okusun (`get_latest()` zaten var, I14'ün `MetricState` deseniyle
tutarlı bir yaklaşım). Yeni thread eklemenin kendi riski var (yaşam
döngüsü, shutdown senkronu) — bunu I9/I10'daki COM/SEH derslerine uygun
şekilde tasarla.

**Alternatif (Faz 0 etkiyi önemsiz bulursa):** Değişiklik yapmadan,
V9 planında "değerlendirildi, etki ihmal edilebilir" notuyla kapat —
J8'in gerçek bir düzeltme mi yoksa aşırı-mühendislik mi olduğuna dürüstçe
karar ver.

### Faz 2-3
Yeni thread ekleniyorsa: shutdown sırasının doğru olduğunu (I9/I10
derslerine uygun) test et, `PipelineCharacterization` regresyon
kontrolü şart (bu proje boyunca sürekli kullanılan davranış-koruma testi).

---

## Genel Sıra ve Commit Disiplini

- Sıra: J6 → J5 → J7 → J8. Her biri kendi Faz 0 raporunu ayrı sunacak,
  bir sonrakine geçmeden onay beklenecek.
- Her madde en az 1 commit; J8 muhtemelen en büyüğü (yeni thread
  ekleniyorsa 2-3 commit).
- `tests/baseline_metrics.txt` asla commit edilmez.
- Push: Sprint 1'deki gibi her madde bağımsız — tamamlanan madde
  hemen push onayına sunulsun, dördünü topluca beklemeye gerek yok.
- Dokümantasyon: her madde kapandığında `FABLE5_BUG_PLAN_V9.md`'de
  durumu güncellenmeli, `SESSION_NOTES.md`'ye ek.
- "Test edildi" / "kod incelemesiyle doğrulandı" / "muhakemeyle kabul
  edilebilir" ayrımı her raporda açık — J8'de özellikle önemli (etki
  büyüklüğü doğrudan ölçülemeyebilir).

Faz 0 bulguları V9 belgesinin varsayımlarıyla çelişirse (örn. J7'nin
zaten güvenli olduğu, J8'in I14 ile çakıştığı/gereksiz olduğu ortaya
çıkarsa) implementasyona geçmeden dur, raporla — kurulu proje deseni.
