# TALİMAT: I8 — obs-websocket Kimlik Doğrulaması (WS Auth)

**Kaynak:** `docs/FABLE5_BUG_PLAN_V8.md` (I8), CONTEXT.md bölüm 2 (Faz 1) ve 5
**Hedef dosya konumu:** Tamamlanınca `docs/talimatlar/` arşivine taşınmalı.

---

## Görevin Özü

`ws_server.rs` şu an kimlik doğrulaması yapmıyor — WS portuna erişebilen
herkes yayını durdurabilir, sahne değiştirebilir. Bu gerçek bir güvenlik
açığı. Hedef: obs-websocket 5.x protokolünün standart auth mekanizmasını
(Hello'da `challenge`+`salt`, Identify'da `authentication` alanı,
`base64(sha256(base64(sha256(parola+salt)) + challenge))`) uyumlu istemcileri
kırmadan implemente etmek.

**Kritik gerilim — baştan bilinçli ele alınacak:** Faz 1'de Identify
**kasıtlı olarak zorunlu tutulmamıştı** (toleranslı handshake, `control.html`'i
kırmamak için). Auth eklemek bu toleransla çelişir. Çözüm ilkesi (Faz 1
tasarımında detaylandırılacak): **parola ayarlanmamışsa mevcut toleranslı
davranış birebir korunur** (geriye dönük uyumluluk, OBS'in kendi davranışıyla
da tutarlı — OBS'te auth opsiyoneldir); **parola ayarlıysa** Identify +
geçerli `authentication` zorunlu olur, doğrulanmamış oturumların istekleri
reddedilir.

**I19/I33 dersi burada da geçerli:** Parolanın *ayarlandığı yer* (Settings UI
veya config) ile Rust'ın *okuduğu yer* arasındaki zincir uçtan uca
doğrulanmalı — Rust tarafını implemente edip UI/config wiring'ini
bağlamamak, yine "hayali özellik" üretir.

---

## Faz 0 — Güncel `master`'a Karşı Yeniden Doğrulama (kod yazmadan, zorunlu)

1. `ws_server.rs`'in güncel handshake akışını haritala: Hello ne gönderiyor,
   Identify geldiğinde/gelmediğinde ne oluyor, istekler hangi durumda kabul
   ediliyor? Toleranslı davranışın tam sınırları neler? (msgpack yolu — Aşama
   7 — dahil: auth her iki serileştirmede de çalışmak zorunda.)
2. `FABLE5_BUG_PLAN_V8.md`'deki I8 tanımını oku; tanım güncel kodla örtüşüyor
   mu teyit et.
3. **Parola kaynağı keşfi:** Projede WS parolası tutabilecek mevcut bir
   ayar/config altyapısı var mı (SettingsDialog, config dosyası, env)?
   `control.html` bir parolayı nereden alabilir/girebilir? Bu keşif, Faz 1'in
   en önemli tasarım girdisi — varsayma, bul.
4. `.claude/skills/obs-ws-protocol/SKILL.md`'yi oku; auth ile ilgili mevcut
   not/kapsam kaydı var mı kontrol et.
5. Mevcut close-code kullanımını kontrol et (4007 NotIdentified, 4008
   AlreadyIdentified, 4009 AuthenticationFailed vb. — hangileri zaten var?).

**Faz 0 çıktısı:** Handshake haritası + parola-kaynağı bulgusu + eski
varsayımlardan sapmalar. Onaya sun, sonra tasarıma geç.

## Faz 1 — Tasarım (implementasyondan önce onaya sunulacak)

1. **Auth durumu makinesi:** oturum başına durum (Handshake → Identified /
   Unauthenticated), parola ayarlıyken doğrulanmamış isteklerin reddi
   (protokole uygun close code'larla: yanlış auth → 4009, Identify'sız istek
   → 4007). Parola yokken mevcut toleranslı akış bit düzeyinde aynı kalır.
2. **Kripto detayları:** sha256 + base64 zinciri, challenge/salt üretimi
   (oturum başına rastgele, kriptografik RNG), parola karşılaştırmasında
   **sabit-zamanlı karşılaştırma**. Yeni bağımlılık gerekiyorsa (örn. `sha2`,
   `base64`, `subtle`) minimal tut ve gerekçelendir.
3. **Parola kaynağı ve yaşam döngüsü:** Faz 0 bulgusuna göre — Settings
   UI'dan mı, config dosyasından mı? Çalışırken değişirse mevcut oturumlara
   ne olur (öner: mevcut oturumlar sürer, yeni bağlantılara yeni parola —
   OBS davranışı)? Parolanın loglara asla yazılmaması kuralı.
4. **`control.html` stratejisi:** Yerleşik istemci parola ayarlıyken nasıl
   bağlanacak? (Seçenekler: parola giriş alanı / yerel istisna YOK — istisna
   koymak açığı geri açar, önerme.) Faz 0 keşfine göre öner.
5. **Kapsam sınırı:** Rate-limiting/brute-force koruması bu maddenin kapsamı
   DIŞINDA — gerekirse V8'e yeni madde olarak öner, bu işi şişirme. TLS/wss
   de kapsam dışı (ayrı altyapı işi).
6. **FFI/UI yüzeyi:** Parola Settings'ten geliyorsa gereken setter + startup
   senkronu (I19 deseni: sinyalin Rust'a ulaştığı find-references ile
   kanıtlanacak).

## Faz 2 — İmplementasyon (küçük commit'ler; push zamanlaması sonda topluca, I33'teki gibi)

Önerilen sıra (Faz 0/1'e göre uyarlanabilir):
1. Auth çekirdeği (Rust): challenge/salt üretimi, doğrulama fonksiyonu
   (saf, birim-test edilebilir), sabit-zamanlı karşılaştırma.
2. Handshake entegrasyonu: parola ayarlıyken Hello'ya `authentication`
   nesnesi + Identify doğrulaması + close code'lar; parolasızken davranış
   değişmez. JSON + msgpack her ikisi.
3. Doğrulanmamış istek reddi (4007 yolu) — yalnız parola ayarlıyken.
4. Parola kaynağı wiring'i (config/UI + startup senkronu).
5. `control.html` uyarlaması (Faz 1 kararına göre).
6. Dokümantasyon: V8 planı, SESSION_NOTES, `obs-ws-protocol` skill'ine auth
   bölümü, ROADMAP Faz 1 nitelemesi güncelle, talimatı arşivle.

## Faz 3 — Test ve Dürüstlük Sınırları

- Rust birim: doğrulama fonksiyonu (doğru/yanlış parola, bozuk base64),
  challenge/salt benzersizliği, sabit-zamanlı yol.
- Entegrasyon (Faz 1 Aşama 6'daki gibi **gerçek istemci kütüphaneleriyle**):
  `obs-websocket-js` ve `simpleobsws` ile (a) parolasız → mevcut davranış
  aynen, (b) doğru parola → bağlanır ve komut çalıştırır, (c) yanlış parola
  → 4009 ile kapanır, (d) msgpack modunda doğru parola → çalışır.
- `control.html` parola ayarlıyken ve ayarlı değilken elle akış kontrolü —
  **GUI/tarayıcı etkileşim onayı kullanıcıda**, raporda "test edildi" değil
  uygun nitelemeyle.
- Kullanıcıya görünen davranış değişikliklerini önden listele (özellikle:
  parola ayarlarsa eski toleranslı istemciler artık reddedilir).

## Sabit Kurallar (hatırlatma)

- Küçük, mantıksal commit'ler; seri tamamlanıp doğrulanınca topluca push,
  push öncesi onay.
- `tests/baseline_metrics.txt` asla commit edilmez.
- Parola hiçbir log/test artefaktına düz metin yazılmaz.
- "Test edildi" / "kod incelemesiyle doğrulandı" / "muhakemeyle kabul
  edilebilir" ayrımı her raporda açık.
- Faz 0 bulguları varsayımlarla çelişirse implementasyona geçmeden dur,
  raporla.
