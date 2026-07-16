# TALİMAT: Eski Planlama Dosyalarının Arşivlenmesi + Makro Motoru Roadmap Notu

**Kaynak:** Kullanıcının yüklediği `memory.md` ve `progress.md` — proje
başlangıcında (2026-05-22 — 2026-06-04 arası) oluşturulmuş, sonrasında hiç
güncellenmemiş planlama dosyaları. CONTEXT.md/SESSION_NOTES.md'nin güncel
proje durumuyla ciddi çelişkileri var (Vulkan'ı henüz alınmamış karar
sanıyor, WGC'den hiç haberi yok, farklı bir versiyon şeması — v0.1-v2.0 —
kullanıyor, farklı bir multi-model analiz aracına — Aider + MiniMax M3 +
Gemini 2.5 Pro — referans veriyor).

**Bu, kod değişikliği değil, saf dokümantasyon/arşivleme işi** — Faz 0-3
döngüsü gerekmiyor, doğrudan uygulanabilir.

---

## İş 1 — Arşivleme

Bu iki dosya (muhtemelen repo kökünde veya `docs/`'ta bir yerde duruyor —
önce find-references ile gerçek konumlarını bul) `docs/archive/`'a
taşınmalı (klasör yoksa oluştur). Her ikisinin başına aşağıdaki uyarı
bloğu eklenmeli:

```markdown
> ⚠️ **ARŞİV — GÜNCEL DEĞİL.** Bu dosya projenin başlangıcında
> (2026-05/06) oluşturuldu ve o tarihten beri hiç güncellenmedi. İçeriği
> güncel proje durumuyla önemli ölçüde çelişiyor (örn. bu dosya Vulkan'ı
> henüz alınmamış bir karar sanıyor, oysa Vulkan/GL interop çoktan
> derinlemesine implemente edilmiş ve I23/K1-K7 turlarında düzeltilmiştir;
> WGC'den hiç bahsetmiyor, oysa WGC aktif capture yoludur — bkz.
> `docs/CONTEXT.md`). Yalnızca projenin ilk niyetinin tarihsel kaydı
> olarak tutulmaktadır. Güncel durum için `docs/CONTEXT.md` ve
> `docs/SESSION_NOTES.md`'ye bakın.
```

Silme, yalnızca taşı + uyarı ekle + commit.

## İş 2 — Makro Motoru'nu ROADMAP.md'ye Taşı

`progress.md`'deki "Macro Engine (v0.5+)" fikri (keybind/trigger macro
sistemi — hotkey/timer/event-driven tetikleyiciler, JSON config,
`~/.reji/macros.json` kalıcılık, aksiyon örnekleri: sahne değiştir,
bitrate ayarla, kayıt aç/kapat) hâlâ geçerli bir fikir, yalnızca hiçbir
zaman güncel roadmap'e taşınmamıştı.

`docs/ROADMAP.md`'ye, "Mimari Not — Dağıtık Mimari Hedefi" bölümünden
sonra (veya uygun bir yere) aşağıdaki bölümü ekle:

```markdown
## Gelecek Fikir — Makro Motoru (henüz taahhüt edilmedi)

Projenin ilk planlama aşamasından (arşivlenmiş `docs/archive/progress.md`)
taşınan bir fikir — hiç implemente edilmedi, şu an aktif roadmap'in
parçası değil, yalnızca kaybolmaması için kayda geçirildi.

**İçerik:** Kullanıcı tanımlı tetikleyicilerle (hotkey, zamanlayıcı,
event-driven — örn. bir healing aksiyonu tetiklendiğinde) çalışan bir
makro sistemi. Örnek aksiyonlar: sahne değiştir, bitrate ayarla, kayıt
aç/kapat.

**Neden şimdi değil:** Reji Studio'nun kontrol yüzeyi zaten obs-websocket
protokolü (Faz 1) üzerinden Stream Deck/Companion gibi araçlara açık —
bu araçların çoğu zaten kendi makro/tetikleyici sistemlerine sahip. Yeni
bir dahili makro motoru eklemeden önce, mevcut obs-websocket yüzeyinin bu
ihtiyacı zaten karşılayıp karşılamadığı değerlendirilmeli (YAGNI —
V9/J-serisinde defalarca uygulanan ilke).

**Olası tasarım (ileride değerlendirilecek, şimdi karar değil):**
- Tetikleyiciler: hotkey (global), zamanlayıcı, healing-event (RuleEngine
  ile entegrasyon ihtimali — I33'ün action-queue mimarisiyle örtüşebilir,
  ayrı bir mekanizma icat etmeden mevcut event akışına eklenebilir mi
  değerlendirilmeli).
- Kalıcılık: JSON config (`rules.json` deseniyle tutarlı bir format
  düşünülebilir).
- Kapsam: Bu bir "ne zaman" veya "nasıl" kararı değil — yalnızca fikrin
  kaybolmaması için buradaki not. Faz 3 (ISource) veya sonrası bir
  noktada, gerçek kullanıcı talebi/ihtiyacı doğarsa gündeme alınabilir.
```

## İş 3 — Diğer Orphan Fikirler (bilgi amaçlı, aksiyon değil)

`progress.md`'de makro motoru dışında da hiç güncel roadmap'e taşınmamış
birkaç fikir var — bunlara şimdi dokunma, yalnızca farkında ol (kullanıcı
isterse ayrı ele alınır):
- Sanal kamera (DirectShow filter, OBS/Teams/Zoom uyumlu)
- OBS sahne import parser
- SQLite oturum kaydı/analitik (metrik geçmişi, trend analizi)
- Plugin sandbox (Extism/WASM) — in-process plugin güvenlik riski
- OSD overlay (D3D11 texture tabanlı, canlı bitrate/fps/temp gösterimi)

**Not:** NDI zaten güncel `ROADMAP.md`'de Faz 4 olarak mevcut — bu fikir
kaybolmamış, doğru taşınmış. Yalnızca yukarıdaki beşi orphan durumda.

## Sabit Kurallar

- Bu iş kod değiştirmiyor, `PipelineCharacterization` vb. regresyon
  gerekmez.
- Tek commit yeterli (arşivleme + ROADMAP.md notu birlikte).
- `SESSION_NOTES.md`'ye kısa bir not düş ("eski planlama dosyaları
  arşivlendi, makro motoru fikri ROADMAP'e taşındı").
