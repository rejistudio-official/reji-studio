# TALİMAT: WS/Auth Görünürlüğü — Port + Aktif Bağlantı Sayısı

**Kaynak:** Ayarlar UX Madde 6, Bölüm C — P1 (en düşük risk, en iyi
maliyet/değer oranı).
**Hedef dosya konumu:** Tamamlanınca `docs/talimatlar/` arşivine
taşınmalı.

---

## Görevin Özü

Settings dialog'un WS/auth bölümünde (I8'de eklenen parola alanının
yanına) iki bilgiyi görünür kılmak:
1. **Dinlenen port** — `rj_get_ws_port()` zaten var ama yalnızca
   `command_router.cpp`'de log'a yazılıyor, UI'a hiç ulaşmıyor.
2. **Aktif bağlantı sayısı** — hiçbir FFI'ı yok, yeni bir sayaç
   gerekiyor.

Bu, düşük risklidir: salt-okunur bilgi gösterimi, mevcut bağlantı
akışına müdahale etmiyor, güvenlik modelini değiştirmiyor.

---

## Faz 0 — Doğrulama (kod yazmadan)

1. `rj_get_ws_port()`'un tam imzasını ve `command_router.cpp`'deki
   kullanım noktasını teyit et (bayat olabilir).
2. `ws_server.rs`'te aktif bağlantı/oturum sayısını izleyen bir yapı
   var mı (örn. `Session` listesi, bir `HashMap`, bir `Arc<Mutex<...>>`)
   — yoksa yeni bir atomic sayaç mı eklenecek?
3. Bağlantı açılış/kapanış noktalarını (`handle_socket`'in başı/sonu
   veya benzeri) bul — sayacın artırılacağı/azaltılacağı doğru yerler
   burada.
4. Settings dialog'daki mevcut WS/auth `QGroupBox`'ının (I8'de eklenen)
   yapısını incele — port/bağlantı bilgisinin nereye ekleneceğini
   netleştir.
5. **Güncelleme sıklığı sorusu:** Bağlantı sayısı UI'da nasıl
   güncellenecek — Settings dialog her açıldığında bir kerelik sorgu mu
   (basit), yoksa canlı/periyodik bir güncelleme mi (daha karmaşık,
   muhtemelen gereksiz karmaşıklık)? Varsayılan öneri: dialog açıldığında
   bir kerelik sorgu — YAGNI, canlı güncelleme gerçek bir ihtiyaç
   olmadıkça eklenmesin.

**Faz 0 çıktısı:** Port/bağlantı-sayısı erişim noktalarının durumu +
güncelleme sıklığı önerisi. Onaya sun (kısa olabilir, küçük bir iş).

## Faz 1 — Tasarım (kısa onay yeterli olabilir)

1. **Yeni FFI (yalnızca bağlantı sayısı için):** `rj_get_ws_connection_count()`
   gibi basit bir `u32` döndüren fonksiyon — atomic sayaçtan okur.
2. **UI:** Settings dialog'un WS grubuna iki salt-okunur alan (`QLabel`):
   "Port: 7070" ve "Aktif bağlantı: 2" gibi. Dialog açıldığında
   (constructor veya `showEvent`) bir kerelik güncellenir.
3. Yeni bir persistence gerekmiyor — bu bilgi kalıcı bir ayar değil,
   anlık durum.

Tasarımı onaya sun, implementasyondan önce.

## Faz 2 — İmplementasyon (küçük, muhtemelen tek commit)

CLAUDE.md Bölüm 8b'ye göre değerlendir — küçük, davranış değiştirmeyen,
güvenlik-hassas olmayan (salt-okunur bilgi) bir değişiklik, muhtemelen
`master`'a doğrudan gidebilir tek commit'le.

## Faz 3 — Test ve Dürüstlük Sınırları

- Birim testi: sayacın bağlantı açılış/kapanışında doğru artıp
  azaldığını doğrula.
- Regresyon: mevcut WS testleri PASS kalmalı.
- **GUI görsel doğrulaması kullanıcıda kalacak** — Settings dialog'u
  açıp port/bağlantı sayısının doğru göründüğünü gözlemlemek.
- Kullanıcıya görünen davranış değişikliği: yalnızca iki yeni
  bilgi alanı, mevcut davranış değişmiyor.

## Sabit Kurallar

- `tests/baseline_metrics.txt` asla commit edilmez.
- "Test edildi" / "kod incelemesiyle doğrulandı" ayrımı raporda açık.
- Bu küçük bir iş — Faz 0/1'i gereksiz uzatma, ama atlamadan hızlıca
  geç.
