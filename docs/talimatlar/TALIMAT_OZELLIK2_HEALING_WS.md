# TALİMAT: Özellik #2 — Healing Durumunu obs-websocket'e Açmak

**Kaynak:** `docs/ROADMAP.md`, "Gelecek Özellikler — Sinerjik Değerlendirme",
madde 2 (I8 + I33 kesişimi).
**Hedef dosya konumu:** Tamamlanınca `docs/talimatlar/` arşivine taşınmalı.

---

## Görevin Özü

Özellik #1'de (CoPilot aksiyon açıklaması) kullanıcı **yerel GUI'de**
neden bir aksiyon önerildiğini görebiliyor artık. Bu özellik aynı bilgiyi
**WS üzerinden dışa** açıyor — bir Stream Deck/Companion paneli healing
durumunu gösterebilsin, hatta (kapsam Faz 1'de netleşecek) kullanıcı
kendi donanım butonuyla pending bir aksiyonu onaylayabilsin.

**Bu, Özellik #1'den daha hassas bir alan** — çünkü salt bilgi
göstermenin ötesine geçip (onaylarsanız) **uzaktan mutasyon** (aksiyon
onaylama) imkânı taşıyor. I8'de kurduğumuz auth mimarisi burada gerçekten
yük taşıyacak.

---

## Faz 0 — Mevcut Mimariyi Çıkar (kod yazmadan, zorunlu)

1. **WS event yayın mekanizmasını haritala:** `ws_evt_tx` broadcast
   kanalının (I15'te hot-path'ten drainer'a taşınmıştı) şu an hangi event
   tiplerini yayınladığını bul. `GetStreamStatus` gibi statik sorguların
   ötesinde, gerçek zamanlı push event'leri var mı (sahne değişimi gibi)?
2. **obs-websocket v5 protokolünün "Vendor Event/Request" mekanizmasını
   araştır** — bu protokol, üçüncü taraf eklentilerin (bizim healing
   sistemimiz gibi) standart obs event/request setinin dışında kalan özel
   veri yayınlaması için resmi bir yol tanımlıyor mu (`web_search` ile
   obs-websocket v5 spec'ini kontrol et, gerekirse). Eğer varsa, bu proje
   bunu implemente etmiş mi, yoksa I8'de kurulan JSON/msgpack zarfı zaten
   kendi (obs-dışı) alanını mı kullanıyor?
3. **I33'ün event kind'larını** (`New`, `Invalidated` — `ui_event_queue`/
   `RjActionEvent`) ve mod değişimi sinyalini (`healingModeChanged`, I19)
   incele — bunları WS'e taşımak için gereken veri zaten Rust tarafında
   mevcut mu?
4. **Auth durumunu netleştir (kritik):** I8'in oturum-düzeyi auth
   bayrağını hatırla — parola ayarlıysa doğrulanmamış bağlantı hiçbir
   Request/`{cmd}` işleyemiyor. Yeni healing event'lerinin **yayını**
   (read-only, mevcut broadcast modeliyle tutarlı, herkes görür) ile
   yeni bir **onay komutu**nun (mutasyon, kimin gönderdiği önemli)
   auth gereksinimleri farklı olabilir — ikisini ayrı ayrı değerlendir.
5. **`control.html`'in bu event'leri gösterip gösteremeyeceğini** kontrol
   et — I8'de control.html obs-auth handshake'ine yükseltilmişti, healing
   event'lerini görüntülemek için ek bir UI elemanı gerekip gerekmediğini
   not et (kapsam kararı Faz 1'e).

**Faz 0 çıktısı:** Vendor Event/Request mekanizmasının varlığı/kullanım
durumu + I33/I19 verisinin WS'e taşınabilirliği + auth ayrımı (yayın vs
mutasyon). Onaya sun.

## Faz 1 — Tasarım (implementasyondan önce onaya sunulacak, iki aşamalı kapsam)

**Aşama A (düşük risk, önerilen ilk hedef): Salt-okunur healing event
yayını.**
- Yeni event tipleri: mod değişimi, pending aksiyon belirdi (açıklama
  dahil — Özellik #1'in verisiyle), aksiyon uygulandı/reddedildi/
  geçersizleşti.
- obs-websocket'in Vendor Event mekanizması varsa onu kullan (protokol-
  idiomatik); yoksa I8'in zaten kurduğu JSON/msgpack zarfını genişlet
  (yeni mimari icat etme, var olanı genişlet).
- Auth: mevcut oturum-düzeyi modele tabi (parola ayarlıysa doğrulanmamış
  bağlantı bu event'leri de almamalı mı, yoksa broadcast bilgi mi
  sayılmalı — Faz 0 bulgusuna göre karar ver ve gerekçele).

**Aşama B (daha yüksek risk, ayrı onay gerekli): Uzaktan onay komutu.**
- Bu, I33'ün `rj_action_approve`/`rj_action_reject` FFI fonksiyonlarını
  WS Request'i olarak açmak demek — **gerçek bir mutasyon yüzeyi**.
- **Zorunlu güvenlik notu:** Bu komut yalnızca parola ayarlıyken VE
  doğrulanmış bağlantıdan kabul edilmeli, istisnasız (I8'in "legacy
  `{cmd}` yolu" dersini hatırla — her giriş noktası auth kapısından
  geçmeli, hiçbiri unutulmamalı).
- Kapsam sorusu: Aşama A ile aynı turda mı yapılsın, yoksa ayrı bir
  talimat mı olsun? **Öneri: ayrı.** Aşama A'nın kendi başına zaten
  tam bir özellik olduğunu, B'nin ek güvenlik yüzeyi taşıdığını göz
  önünde bulundurarak, A'yı bitirip kullanıcı gözlemiyle doğruladıktan
  sonra B'ye geçmek daha güvenli bir kademelendirme olur — bunu Faz 1
  raporunda öner, kullanıcı onaylasın.

Bu talimatın kapsamı **yalnızca Aşama A**'dır (öneri). Aşama B onay
alırsa ayrı bir takip talimatı yazılacak.

## Faz 2 — İmplementasyon (küçük commit'ler, Aşama A)

Faz 1 onayına göre uyarlanacak, örnek iskelet:
1. Rust: yeni event tiplerinin tanımı + `RuleEngine`/action-queue'dan
   bu event'lerin üretimi (I33'ün mevcut event üretim noktalarına ek).
2. FFI/WS: event'lerin `ws_evt_tx` üzerinden (veya Vendor Event
   mekanizmasıyla) yayınlanması, JSON + msgpack ikisinde de.
3. `ffi-safety-review` prosedürü, yeni struct/enum varsa.
4. Dokümantasyon: `docs/talimatlar/`'daki I8 ve I33 notlarına çapraz
   referans, `FFI_CONTRACT.md`, `ROADMAP.md` madde 2 → "Aşama A
   implemente edildi", `SESSION_NOTES.md`.

## Faz 3 — Test ve Dürüstlük Sınırları

- Entegrasyon (I8'in Aşama 6 tarzı gerçek istemci testleri emsali):
  `obs-websocket-js`/`simpleobsws` ile yeni event'lerin gerçekten
  alınabildiğini doğrula — bu proje daha önce bu yöntemle 2 gerçek
  üretim hatası (Faz 1) yakalamıştı, aynı titizlik burada da geçerli.
- Auth senaryosu: parolasız/parolalı/yanlış-parola bağlantıların yeni
  event'lere erişiminin Faz 1'de kararlaştırılan modele uyduğunu test
  et.
- Regresyon: mevcut WS testleri (I8'den beri birikmiş) PASS kalmalı.
- Kullanıcıya görünen davranış değişikliği: yeni event'ler yayınlanıyor
  olması dışında mevcut davranış değişmemeli — bunu açıkça belirt.
- GUI/gerçek Stream Deck gözlemi muhtemelen kullanıcıda kalacak (I8'deki
  fiziksel donanım testi gibi).

## Sabit Kurallar (hatırlatma)

- Küçük, mantıksal commit'ler; tamamlanınca push öncesi onay.
- `tests/baseline_metrics.txt` asla commit edilmez.
- "Test edildi" / "kod incelemesiyle doğrulandı" / "muhakemeyle kabul
  edilebilir" ayrımı her raporda açık.
- Faz 0 bulguları bu talimatın varsayımlarıyla (özellikle Vendor Event
  mekanizmasının var/yok olması) çelişirse dur, tasarımı buna göre
  yeniden çerçevele, raporla.
- Auth konusunda özellikle temkinli ol — I8'in kurduğu güvenlik modelini
  bu yeni event/komut yüzeyinde de eksiksiz uygulamak zorunlu, "bilgi
  vermek zararsız" varsayımıyla auth kontrolünü atlama.
