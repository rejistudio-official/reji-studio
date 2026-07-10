# Talimat — V8/I28: `oldLayout=UNDEFINED` Validation Layer Doğrulaması

**Bu talimat kod değişikliği İÇERMİYOR.** `copy_optimizer.cpp`'deki `D2`/`E4`
yorumları (satır 200-210), bu tasarımın kasıtlı olduğunu gösteriyor —
"D3D11 dışarıdan yazdı, keyed mutex sahipliği Vulkan'a devretti" argümanı.
Bu, Vulkan'ın external memory queue-family-ownership-transfer semantiğine göre
**makul olabilir** — ama kesin cevabı sadece gerçek validation layer verir.
Kod okuyarak "doğru/yanlış" kararı vermek bu noktada spekülasyon olur.

**Bu talimat sizin (kullanıcının) dual-GPU donanımında çalıştırmanız gereken
adımları içeriyor** — Claude Code'un tek başına yapabileceği kısım sınırlı
(ortam hazırlığı + sonuç yorumlama), gerçek çalıştırma AMD+NVIDIA donanım
gerektiriyor.

## Claude Code'un yapabileceği kısım (otonom)

1. `.claude/skills/vulkan-interop-debug/SKILL.md`'deki standart validation
   layer prosedürünü doğrula — hâlâ güncel mi (`VK_INSTANCE_LAYERS=
   VK_LAYER_KHRONOS_validation` hâlâ doğru env var adı mı, proje CMake'inde
   debug build'de validation layer'ın otomatik linklendiği bir mekanizma
   var mı, yoksa elle mi açılıyor).
2. `copy_optimizer.cpp`'nin D2/E4 yorumlarının **tam olarak hangi VUID'i**
   hedef aldığını (ya da hedeflemesi gerektiğini) araştır — Vulkan spec'inin
   `VK_QUEUE_FAMILY_EXTERNAL` acquire/release semantiğini ele alan bölümünü
   (queue family ownership transfer) oku, `oldLayout=UNDEFINED`'in bir
   ACQUIRE barrier'ında (release tarafının ne olduğu bilinmeyen, harici bir
   kaynak için) spec-uyumlu olup olmadığına dair **spec metninden alıntıyla**
   bir ön-değerlendirme yaz (kesin karar değil, ön-hazırlık).
3. Aşağıdaki test prosedürünü, gerçek proje komutlarıyla (build hedefi,
   log yolu vb.) somutlaştırıp `docs/SESSION_NOTES.md`'ye bir "I28 Test
   Prosedürü" bölümü olarak ekle — böylece siz sadece komutları çalıştırırsınız,
   hangi komutun ne olduğunu tekrar bulmanıza gerek kalmaz.

## Sizin (kullanıcı) çalıştırmanız gereken kısım

```bash
# 1. Validation layer'ı aç
set VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation
# (Claude Code'un doğrulayacağı gerçek env var adını kullan)

# 2. Debug output'u yakala (GUI'de stderr kaybolduğu için — RTMP'de
#    kullandığımız REJI_RTMP_LOG deseninin benzeri bir mekanizma varsa
#    onu kullan, yoksa DBWIN/OutputDebugString dinleyicisi)
just run > vulkan_validation_output.txt 2>&1
# veya DebugView (Sysinternals) ile canlı izle

# 3. En az 10-15 saniye normal çalıştır (birkaç sahne değişimi, preview
#    aktifken) — VUID hataları genelde ilk birkaç karede veya sahne
#    geçişinde tetiklenir

# 4. Çıktıda ara:
findstr /i "VUID VkImageMemoryBarrier oldLayout" vulkan_validation_output.txt
```

## Beklenen iki sonuç, ikisi de bilgilendirici

- **VUID hatası YOKSA:** D2/E4 yorumundaki tasarım gerekçesi doğrulanmış
  demektir — I28 `[KAPANDI: validation layer temiz, kasıtlı tasarım
  doğrulandı]` olarak işaretlenir, kod değişikliği gerekmez.
- **VUID hatası VARSA:** Opus'un bulgusu doğrulanmış demektir — hatanın
  TAM METNİNİ (VUID kodu dahil) paylaşın, ben ona göre gerçek bir düzeltme
  talimatı yazarım (muhtemelen `oldLayout`'u değiştirmek değil, çünkü D2/E4
  yorumunun neden yanlış olduğunu VUID'in kendisi açıklayacak — bazen çözüm
  `oldLayout=UNDEFINED` yerine gerçek bir "release" barrier'ının D3D11
  tarafında da (mümkünse) kurulması olabilir, bazen gerçekten `oldLayout`
  değişikliği olabilir — VUID metni hangisi olduğunu söyler).

## Doğrulama Checklist (Claude Code için)

- [ ] `vulkan-interop-debug` skill'indeki prosedür güncelliği doğrulandı
- [ ] D2/E4'ün hedeflediği VUID'e dair spec-alıntılı ön-değerlendirme yazıldı
- [ ] `docs/SESSION_NOTES.md`'ye somut "I28 Test Prosedürü" bölümü eklendi
      (gerçek komutlar, gerçek log yolu)
- [ ] Commit: `docs: V8/I28 — validation layer test prosedürü hazırlığı`
- [ ] Push yapma — hazırlığı raporla; gerçek test SİZİN donanımınızda,
      sonucu siz paylaşınca ben değerlendiririm

## Sınır

Bu talimat gerçek testi ÇALIŞTIRMIYOR — sadece hazırlıyor. Gerçek
çalıştırma ve sonuç yorumlaması, sonraki bir adım.
