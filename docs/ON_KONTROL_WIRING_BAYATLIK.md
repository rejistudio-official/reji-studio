# ÖN-KONTROL: ExistingDesktopSource Wiring Talimatını Başlatmadan Önce

**Amaç:** docs/talimatlar/TALIMAT_EXISTINGDESKTOPSOURCE_WIRING.md
20 Temmuz'da yazıldı. O tarihten bugüne (29 Temmuz) V10 Sprint 1-3
tam da capture/healing bölgesine dokunan üç büyük değişiklik yaptı.
Bu talimatı olduğu gibi başlatmadan önce, bu değişikliklerin wiring
planını nasıl etkilediğini netleştir. Bu adım kod yazmaz — yalnızca
talimatın güncelliğini ve açık kararlarını yeniden değerlendirir.

---

## Kontrol Edilecek Üç Etkileşim

1. frame_drop_policy.h (S1-ek4) ile ilişki. Bu düzeltme,
   null-frame'in artık drop sayılmadığını garantiledi — ama
   CaptureSubsystem::handle_null_frame()'in kendi null-streak
   sayacı (reinit eşiği, edge semantiği) bundan etkilenmedi mi?
   Wiring talimatının "kritik uyarı"sı tam bu noktaya değiniyordu:
   handle_null_frame()'in edge semantiği (eşikte bir kez true)
   ile ISource::state()'in level semantiği (sürekli durum)
   arasındaki fark. Bu ayrım hâlâ doğru mu, yoksa S1-ek4 bu ikisinden
   birini değiştirdi mi? Kod incelemesiyle kesinleştir.
2. L5'in init birleştirmesi (wireUpPipeline/startFrameThread)
   ile ilişki. Wiring turu run_frame()'i değiştirecek —
   run_frame()'in çağrıldığı thread/init sırası, L5'in
   birleştirdiği tek init yoluyla hâlâ tutarlı mı? Wiring planının
   varsaydığı çağıran sıra hâlâ geçerli mi?
3. SRT testinde gözlemlenen "Capture loss detected (60 frames) —
   reinit" davranışı (V10'da not düşülmüştü, "boşta reinit eşiği").
   Bu davranış, wiring sonrası ISource::state()'in NeedsReinit
   sinyaline nasıl haritalanacak? Mevcut null-streak eşiği (60 kare)
   ile ISource::state()'in level-semantiği arasındaki dönüşüm,
   V10'da gözlemlenen bu senaryoyu (statik ekranda hızlı eşik-aşımı)
   doğru ele alacak mı, yoksa gereksiz reinit fırtınasına mı yol açar?

## Görevin Özü

1. Yukarıdaki üç noktayı kod incelemesiyle netleştir.
2. Wiring talimatının hâlâ geçerli olan kısımlarını ve
   güncellenmesi gereken kısımlarını ayır.
3. Eğer talimat büyük ölçüde geçerliyse, bunu kısaca teyit edip
   normal Faz 0'ına (talimatın kendi CaptureSubsystem kararı dahil)
   geç.
4. Eğer önemli bir çelişki/güncelleme ihtiyacı bulursan, kod
   yazmadan önce raporla ve onaya sun — talimatı revize etmek
   gerekebilir.

## Sabit Kurallar

- Bu adımda kod yazma — yalnızca teşhis/güncellik kontrolü.
- Bulgunun "talimat hâlâ geçerli" mi "güncellemesi gerekiyor" mu
  olduğunu net bir şekilde ayır.
- Bu kontrol tamamlanıp onaylanmadan wiring'in gerçek
  implementasyonuna (Faz 2) geçme.
