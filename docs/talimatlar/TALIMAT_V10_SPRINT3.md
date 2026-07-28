# TALİMAT: V10 Sprint 3 — L13-L20 (Düşük Öncelik/Hijyen)

**Kaynak:** docs/FABLE5_BUG_PLAN_V10.md, Sprint 2'nin (L8-L12+L21-L23,
dbf4bc8'te merge edildi) ardından son grup. Bu sprint tamamlanınca
V10 planı kapanır — TALIMAT_V10_TARAMA_HAZIRLIK.md da arşivlenebilir
hale gelir.
**Hedef dosya konumu:** Tamamlanınca ilgili talimat dosyaları
docs/talimatlar/ arşivine taşınmalı.

---

## Görevin Özü

Sekiz madde, düşük öncelikli/hijyen sınıfında — Sprint 1/2'deki gibi
büyük mimari riskler taşımıyorlar. Yine de her biri kendi Faz 0'ından
geçsin (rapor iddiası kanıt değil ilkesi burada da geçerli), ama süreç
daha hafif olabilir — çoğu tek satırlık/küçük düzeltmeler.

Canlı-doğrulama gereken maddeleri işaretledim (aşağıda 🔍) —
geri kalanlar saf kod hijyeni, birim testiyle yeterli.

1. L13 — rules_buf 64KB aşımında yanıltıcı "Kural okunamadı".
   Boyut-aşımı ile "motor hazır değil" durumunu ayıran farklı bir
   hata mesajı/kod yolu ekle.
2. L14 🔍 — HealingLog writer thread'e shutdown sinyali + son
   flush. Düzenli kapanışta son ~250ms'lik healing log kaybı riski.
   Düzeltme sonrası: uygulamayı normal kapat, log dosyasında son
   olayların kaybolmadığını doğrula.
3. L15 — rj_action_approve kuyruk-dolu geri koymada created
   tazelenmeli. Onaylanan bir aksiyonun kuyruğa geri konurken eski
   zaman damgasını taşıması, anında TTL'e düşme riski yaratıyor.
4. L16 — pcm_scratch_.reserve init'te. Hot-path'teki realloc'u
   önceden ayırarak engelle.
5. L17 — updateParamSet dupe başarısızlığında bool dönüş.
   Sessiz başarısızlık yerine çağırana bilgi taşı.
6. L18 🔍 — Profil önerisi diyaloğunda vendor/VRAM eşleşmezliği.
   Hibrit-GPU'da display vendor (iGPU) + max VRAM (dGPU) yan yana
   yanıltıcı gösterilebiliyor (örn. "Intel 12GB" gibi anlamsız bir
   kombinasyon). Düzeltme sonrası: ilk-kurulum diyaloğunu tekrar
   tetikleyip (Sprint 1/L5'te kullandığımız profile/asked bayrağını
   temizleme yöntemiyle) gösterimin artık tutarlı olduğunu gözle
   doğrula.
7. L19 — AudioRing dropped_ sayacının doluluk/geçersiz-girdi
   ayrımı. İki farklı "drop" sebebinin aynı sayaca karışması —
   ayrıştır.
8. L20 — hot_reload throttle "Ok ama skip" sözleşmesi. Fiilen
   ölü kod ama gelecekte tuzak — dönüş değerini ayırt edilebilir yap.

---

## Sıra ve Gruplama Önerisi

Bağımsız maddeler — Faz 0'da hangilerinin tek bir küçük dalda
birleştirilebileceğine (örn. tümü saf hijyen, davranış değiştirmeyen
maddeler tek dalda; L14/L18 gibi görünür davranış değiştirenler ayrı)
karar ver ve gerekçele. CLAUDE.md Bölüm 8b'ye göre değerlendir — çoğu
küçük ve davranış-nötr olduğundan doğrudan master'a uygun olabilir,
ama kendi değerlendirmeni yap (önceki sprintlerde bu tahminler bazen
yanlış çıkmıştı).

## Sabit Kurallar

- Her madde kendi Faz 0'ından geçsin, ama süreç orantılı — küçük
  bulgular için uzun analiz gerekmiyor.
- 🔍 işaretli maddeler için canlı GUI doğrulaması iste, diğerleri
  birim testiyle kapanabilir.
- tests/baseline_metrics.txt asla commit edilmez.
- "Test edildi" / "kod incelemesiyle doğrulandı" ayrımı raporda açık.
- Bu sprint bittiğinde: FABLE5_BUG_PLAN_V10.md'yi "TAMAMEN KAPANDI"
  olarak işaretle, TALIMAT_V10_TARAMA_HAZIRLIK.md'yi arşive taşı,
  CONTEXT.md'yi mühürle — V10'un tam kapanışı bu sprintle olacak.
