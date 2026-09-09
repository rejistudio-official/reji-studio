# rules.json / rules.toml Şeması

**Statü:** Spesifikasyon — tek doğruluk kaynağı kod
(`src/orchestrator/src/rules.rs`); bu belge kodun davranışını yansıtır.
Kod ile çelişki hâlinde kod esastır ve belge güncellenir.

**Kapsam:** Kural motorunun (RuleEngine) okuduğu kural dosyasının tam
şeması: alanlar, geçerli değerler, condition grameri, param semantiği ve
— en önemlisi — **sessiz başarısızlık** yolları (Bölüm 8). Kural
dosyası yazarken önce Bölüm 8'i okuyun: motor, hatalı girdilerin çoğunu
reddetmek yerine sessizce yok sayar.

---

## 1. Dosya konumları ve yükleme

| Konum | Rol |
|---|---|
| `%USERPROFILE%\.reji\rules.json` | Çalışma zamanında yüklenen kullanıcı dosyası |
| `docs/config/rules.json.template` | Varsayılan şablon (kaynak) |
| `docs/config/profiles/{performance,stability,efficiency}.json` | Donanım profilleri — aynı şemanın üç örneği |

Yükleme sırası (`hot_reload`, `rules.rs:389`):

1. Dosya içeriği önce **JSON** olarak denenir (`RuleFileJson`).
2. JSON parse edilemezse **TOML** denenir (`RuleFileTOML`).
3. İkisi de başarısızsa hata döner; **eski kurallar bellekte kalır**
   (rollback — hatalı dosya aktif kural setini bozmaz).
4. Doğrulama: her kuralda `id`, `condition`, `action` **boş olmamalı**.
   Doğrulama bundan ibarettir — içerik geçerliliği (metrik adı, action
   adı, mode adı) yükleme anında **kontrol edilmez** (bkz. Bölüm 8).

Hot-reload korumaları: son reload'dan <1 sn geçtiyse dosyaya hiç
bakılmaz (`SkippedThrottled`); mtime değişmemişse okuma/parse atlanır
(`SkippedUnchanged`). Dönüş tipi `ReloadOutcome` üç yolu ayırt eder.

## 2. Üst düzey şema

### JSON (`RuleFileJson`, `rules.rs:652`)

| Alan | Tip | Zorunlu | Varsayılan | Not |
|---|---|---|---|---|
| `rules` | dizi | **Evet** | — | Kural nesneleri (Bölüm 3) |
| `hysteresis_ms` | u64 | Hayır | `0` | Aynı kuralın iki tetiklenmesi arası minimum süre |
| `default_mode` | string | Hayır | `""` | **Şemada var ama OKUNMUYOR** — bkz. Bölüm 8, SB-7 |

### TOML (`RuleFileTOML`, `rules.rs:664`)

| Alan | Tip | Zorunlu | Not |
|---|---|---|---|
| `rules` | dizi | **Evet** | JSON ile aynı kural şeması |
| `[metadata].hysteresis_ms` | u64 | Hayır | ⚠ JSON'da üst düzeyde, TOML'da `[metadata]` altında — konum tutarsız |
| `[metadata].default_mode` | string | Hayır | JSON'daki gibi okunmuyor |

Her iki formatta da **bilinmeyen alanlar sessizce yok sayılır**
(serde'de `deny_unknown_fields` yok) — alan adındaki yazım hatası
(`hystersis_ms`, `Rules`) hata üretmez, alan varsayılanına düşer.

## 3. Kural alanları (`Rule`, `rules.rs:142`)

| Alan | Tip | Zorunlu | Varsayılan | Açıklama |
|---|---|---|---|---|
| `id` | string | **Evet**, boş olamaz | — | Benzersiz kimlik; hysteresis, cooldown ve UI bu ID üzerinden izler |
| `description` | string | Hayır | `""` | Yalnız insan için; motor okumaz |
| `condition` | string | **Evet**, boş olamaz | — | Bölüm 4'teki gramer |
| `action` | string | **Evet**, boş olamaz | — | Bölüm 6'daki 7 değerden biri; yüklemede doğrulanır (SB-5 düzeltmesi, 86fada3) |
| `params` | nesne | Hayır | `{}` | Aksiyona göre anlamlı anahtarlar (Bölüm 6) |
| `modes` | string dizisi | **Evet** (serde varsayılanı yok; eksikse parse hatası) | — | Bölüm 7'deki tam string'ler; içerik doğrulanmaz (SB-6) |

## 4. `condition` grameri

```
koşul     := yaprak | koşul "&&" koşul | koşul "||" koşul
yaprak    := metrik_adı OP tamsayı
OP        := ">=" | "<=" | ">" | "<" | "=="
```

- Öncelik (düşükten yükseğe): `||` → `&&` → yaprak. Yani
  `a > 1 && b > 2 || c > 3` ≡ `(a > 1 && b > 2) || (c > 3)`.
- **Parantez DESTEKLENMEZ** — parantezli koşul hata vermez, sessizce
  hiç tetiklenmeyen kurala dönüşür (SB-3).
- Eşik **i32 tamsayı** olmalı; ondalık (`0.5`), birimli (`85C`) veya
  boş eşik sessizce `false` üretir (SB-2).
- `!=`, `!`, birim/ondalık karşılaştırma yok.
- Değerlendirme: `eval_condition_calibrated` (`rules.rs:164`),
  yaprak ayrıştırma: `parse_leaf` (`rules.rs:214`).

## 5. Geçerli metrikler

`metric_value_and_id` (`rules.rs:195`) tablosundaki 8 ad — başka ad
tanınmaz. `metric_id` sütunu FFI kontratının parçasıdır
(`RjMetricId` ile senkron, bkz. `FFI_CONTRACT.md`).

| `condition`'daki ad | `metric_id` | Kaynak tip | Birim |
|---|---|---|---|
| `frame_drop_pct` | 0 | u32 | % |
| `gpu_temp_c` | 1 | i16 | °C — ⚠ stub guard, bkz. Bölüm 8 SB-8 |
| `cpu_temp_c` | 2 | i16 | °C |
| `memory_usage_pct` | 3 | u32 | % |
| `cpu_load_pct` | 4 | u32 | % |
| `gpu_load_pct` | 5 | u32 | % |
| `network_rtt_ms` | 6 | u16 | ms |
| `network_loss_pct` | 7 | u8 | % |
| — (`NONE`) | 8 | — | "açıklama yok" göstergesi; condition'da kullanılamaz |

## 6. Geçerli aksiyonlar ve `params` semantiği

`create_action` (`rules.rs:547`) tam 7 string tanır:

| `action` | Okunan param | Aksiyondaki karşılık | `is_critical` |
|---|---|---|---|
| `bitrate_reduce` | `step_kbps` (tamsayı) | `param1` = kbps adımı | **Evet** |
| `bitrate_recover` | `step_kbps` (tamsayı) | `param1` = kbps adımı | Hayır |
| `scale_resolution` | `scale_factor` (ondalık) | `param1` = faktör × 1000 (sabit nokta) | **Evet** |
| `restore_resolution` | `scale_factor` (ondalık, vars. `1.0`) | `param1` = faktör × 1000 | Hayır |
| `cap_fps` | `fps_limit` (tamsayı) | `param2` = FPS limiti | Hayır |
| `restore_fps` | — | — | Hayır |
| `log_only` | — | yalnız log, aktüatöre gitmez | Hayır |

Param kuralları:

- `step_kbps` eksik veya sayı-dışıysa → `param1 = 0` (sessiz; SB-9).
- `scale_factor` eksik veya sayı-dışıysa → `1.0` (tam çözünürlük).
- `fps_limit` her aksiyonda `param2`'ye okunur ama yalnız `cap_fps`'te
  anlamlıdır.
- `params` içindeki tanınmayan anahtarlar sessizce yok sayılır.

Not: `Action.require_approval` alanı `!is_critical` olarak set edilir
ama **hiçbir tüketici tarafından okunmaz** — CoPilot onay kararı
`healing.rs`'teki `needs_approval` (mod + kategori) ile ayrıca verilir.
Kural yazarken bu alana güvenmeyin.

## 7. `modes` — geçerli değerler

Tam string'ler (`healing.rs:537-542` `mode_str` eşlemesi; büyük/küçük
harf duyarlı, **tire ile**):

| String | HealingMode | Motor çağrılır mı? |
|---|---|---|
| `"auto-pilot"` | AutoPilot | Evet — aksiyonlar otomatik uygulanır |
| `"co-pilot"` | CoPilot | Evet — kategoriye göre onay/pending |
| `"assist"` | Assist | Evet — yalnız `is_critical` aksiyonlar otomatik, gerisi loglanır |
| `"manual"` | Manual | **Hayır** — motor hiç değerlendirilmez; kurala `"manual"` yazmak anlamsızdır |

Geçersiz string (`"auto"`, `"co_pilot"`, `"Auto-Pilot"` …) hata
üretmez — kural o modda **hiçbir zaman eşleşmez** (SB-6). Boş `modes`
listesi (`[]`) de doğrulamadan geçer ve kuralı fiilen kapatır.

## 8. Sessiz başarısızlıklar ⚠

**Bu bölüm şemanın en kritik parçasıdır.** Motor "fail fast" değil
"sessizce yut" felsefesiyle yazılmıştır; aşağıdaki durumların çoğu
kullanıcıya hata olarak yansımaz. "Kuralım neden çalışmıyor?" sorusunun
cevabı neredeyse her zaman bu tablodadır. **DÜZELTİLDİ** işaretli
maddeler tarihsel kayıt olarak korunur — eski davranış artık geçerli
değildir.

| # | Girdi | Ne olur | Kod |
|---|---|---|---|
| SB-1 | Bilinmeyen metrik adı (`gpu_temp > 85` — doğrusu `gpu_temp_c`) | Yaprak sessizce `false` → kural hiç tetiklenmez. Yüklemede kontrol yok, çalışmada log yok. | `rules.rs:189` (`unwrap_or(false)`), `rules.rs:195` |
| SB-2 | Eşik tamsayı değil (`> 0.5`, `> 85C`, eşik boş) veya operatör yok (`log_always`) | Yaprak sessizce `false` → kural hiç tetiklenmez. | `rules.rs:279-281`, `rules.rs:189` |
| SB-3 | Parantezli koşul (`(cpu_load_pct > 80) && ...`) | `(cpu_load_pct` bilinmeyen metrik sayılır → yaprak `false` → kural sessizce ölür. Parantez desteklenmiyor ama hata da üretmiyor. | `rules.rs:214-223` |
| SB-4 | `&&` ile `\|\|` karışımında beklenmeyen gruplama | Hata değil ama tuzak: gruplama her zaman `(… && …) \|\| (… && …)`; parantezle değiştirilemez (SB-3). | `rules.rs:171-187` |
| SB-5 | Bilinmeyen `action` string'i (`"reduce_bitrate"` — doğrusu `"bitrate_reduce"`) | **DÜZELTİLDİ (86fada3).** Eski davranış: yüklemede geçer, kural ilk tetiklendiğinde `create_action` hatası `?` ile yayılıp o tick'teki tüm sağlıklı aksiyonları da düşürürdü. Yeni davranış: bilinmeyen action **yüklemede reddedilir** (kural id + action değeriyle; rollback eski kuralları korur, `rj_validate_rules` da artık bu dosyayı reddeder). Savunma katmanı: `evaluate` yine de tek kuralın hatasında kuralı atlayıp loglar, kalanları çalıştırır. | `rules.rs` (`action_type_from_str`, yükleme doğrulaması, `evaluate` atla-devam) |
| SB-6 | Geçersiz `modes` değeri (`"auto"`, `"co_pilot"`, büyük harf) veya boş `[]` | Kural o modda hiçbir zaman eşleşmez; uyarı yok. Doğrulama `modes` içeriğine bakmaz. | `rules.rs:466`, `rules.rs:430-434` |
| SB-7 | `default_mode` alanını değiştirmek | **Hiçbir etkisi yok.** Alan her iki formatta da şemanın parçası ama kod okumuyor (`#[allow(dead_code)]`); aktif mod FFI'dan (`rj_set_healing_mode`) gelir. | `rules.rs:656-659`, `rules.rs:673-675` |
| SB-8 | `gpu_temp_c` geçen herhangi bir kural, GPU termal okuma stub'ken (metrik `0`) | Kural **tamamen atlanır** — substring kontrolü: bileşik koşulda (`cpu_load_pct > 80 && gpu_temp_c < 70`) diğer yapraklar da değerlendirilmez. Yalnız `debug!` log. Gerçek termal okuma gelince guard kendiliğinden kalkar. | `rules.rs:500-503` |
| SB-9 | `params` değeri yanlış tipte (`"step_kbps": "500"` — string) | `as_i64()` → `None` → `param1 = 0` → aksiyon fiilen no-op'a yakın çalışır. `scale_factor` yanlış tipteyse sessizce `1.0`. | `rules.rs:564-578` |
| SB-10 | Üst düzey/kural alan adında yazım hatası (`hystersis_ms`, `Modes`) | Bilinmeyen alan sessizce yok sayılır (`deny_unknown_fields` yok); alan varsayılanına düşer. `modes` gibi zorunlu alanda bu, parse hatası olarak yakalanır — opsiyonel alanlarda yakalanmaz. | `rules.rs:651-676` |
| SB-11 | Bozuk JSON dosyası | Hata mesajı yanıltır: JSON parse hatası yutulup TOML denenir; kullanıcıya **TOML hatası** gösterilir ("Cannot parse rules as JSON or TOML: …"). JSON'daki asıl hata (eksik virgül vb.) mesajda görünmez. | `rules.rs:420-427` |
| SB-12 | Doğrulamadan geçemeyen dosya ile tekrar `hot_reload` | **DÜZELTİLDİ (8401d8b).** Eski davranış: mtime doğrulamadan önce kaydedildiğinden, başarısız reload sonrası dosya değişmeden yapılan çağrılar `Ok(SkippedUnchanged)` dönerdi. Yeni davranış: mtime yalnız başarılı yüklemede güncellenir; tekrar deneme hatayı görünür tutar. **Nüans:** bu hata hiç canlı tetiklenmedi (latent) — tek üretim çağıranı `RuleEngine::new` idi ve `rj_reload_rules` her reload'da taze engine kurduğundan bayat mtime atılan engine'le ölüyordu. Gelecekteki watcher/periyodik çağıran için sertleştirme. | `rules.rs` (`hot_reload` mtime bloğu) |
| SB-13 | `\|\|` koşulunda ikinci yaprak tetiklerken UI açıklaması | Açıklama her zaman **ilk yaprağın** metrik/eşiğini gösterir (Faz 1 kararı) — tetikleyen gerçek yaprak farklıysa açıklama yanıltıcı olabilir. | `rules.rs:229-238`, `rules.rs:257` |
| SB-14 | `bitrate_recover` kuralı, kurtarılacak düşüş yokken tetiklenirse | Aksiyon sessizce düşürülür (kasıtlı — sahte healing banner'ı önlenir); yalnız `debug!` log. | `healing.rs:554-569` |

## 9. Çalışma zamanı davranışı (şemayı etkileyen)

- **Hysteresis:** `hysteresis_ms > 0` ise aynı kural (ID bazlı) pencere
  dolmadan tekrar tetiklenmez (`rules.rs:484-491`).
- **Reject cooldown:** CoPilot'ta reddedilen aksiyonun kuralı, cooldown
  süresince değerlendirme dışıdır (`rules.rs:473-481`, V8/I33b).
- **Kalibrasyon (Özellik#5):** Çalışma zamanı kalibre eşiği,
  `condition`'daki literal eşiği **şeffaf override eder** — dosya şeması
  değişmez, dosyadaki değer aynı kalır ama motorun kullandığı eşik
  kayar. UI açıklaması kalibre değeri `calibrated=true` bayrağıyla
  gösterir (`rules.rs:97-102`, `rules.rs:287`).
- **Çakışma çözümü:** Aynı tick'te `reduce`+`recover` →
  yalnız reduce; `cap_fps`+`restore_fps` → yalnız cap;
  `scale`+`restore_resolution` → yalnız scale. Uygulama sırası:
  BitrateReduce → CapFps → ScaleResolution → BitrateRecover →
  RestoreFps → RestoreResolution → LogOnly (`rules.rs:623-648`).

## 10. Geçerli örnek

```json
{
  "rules": [
    {
      "id": "frame_drop_high",
      "description": "Frame drop > 10%, aggressive reduction",
      "condition": "frame_drop_pct > 10",
      "action": "bitrate_reduce",
      "params": { "step_kbps": 500 },
      "modes": ["auto-pilot", "co-pilot", "assist"]
    }
  ],
  "hysteresis_ms": 10000
}
```

Tam örnekler: `docs/config/rules.json.template` (7 kurallı varsayılan
set) ve `docs/config/profiles/*.json` (üç profil; Rust testleri
`rules.rs`'te bu dosyaları şema doğrulamasından geçirir).
