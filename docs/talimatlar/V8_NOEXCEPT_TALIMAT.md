# Claude Code Talimatı — V8 Ek Madde: `ITransport` Arayüzünü `noexcept` ile Sağlamlaştır

**Kaynak:** 06.07.2026 taze taramaları (Fable5 6.1, Opus 4.8 5.3) — ikisi de bağımsız
olarak Faz2'deki SEH virtual-call kararını (`SrtTransport`/`RtmpTransport::shutdown()`'ın
`__try` içinde çağrılması) eleştirdi. Bizim throw-deneyimiz (Faz2/Aşama1) bugünkü iki
implementasyon için geçerli bir ampirik kanıttı, ama **yapısal bir garanti değil** —
gelecekteki her yeni `ITransport` implementasyonu için tekrar elle test edilmesi
gerekirdi. Bu talimat, garantiyi tip sistemine taşıyor.

**Kapsam dışı:** Vulkan/GPU/I2-I3 senkronizasyon işine HİÇ dokunmuyor — tamamen
`output/` katmanında, izole bir sağlamlaştırma.

## Tasarım

1. `ITransport::send`/`shutdown` arayüz imzasına `noexcept` ekle.
2. `noexcept` işaretli bir fonksiyonda exception fırlatılırsa C++ `std::terminate()`
   çağırır — SEH `__try`'a hiç ulaşmaz, "SEH gerçekten yakalar mı" belirsizliği
   ortadan kalkar (terminate net ve öngörülebilir bir başarısızlık, sessiz UB değil).
3. Her implementasyon (`SrtTransport`, `RtmpTransport`) kendi içinde olası
   exception'ları (varsa) yakalayıp `bool`/`void` sözleşmesine çevirmek ZORUNDA
   kalır — yeni bir implementasyon yazan biri bunu atlayamaz (arayüz sözleşmesi).
4. Opus'un ek önerisi: SEH'i (`pipeline.cpp`/`output_subsystem.cpp`'deki dış
   sarmalayıcı) olduğu gibi bırak — kaldırmak kapsam dışı, ayrı bir karar. Bu
   talimat sadece `noexcept` ekliyor, SEH mimarisini yeniden düzenlemiyor.

## Yapılacaklar

### 1. `i_transport.h`

```cpp
virtual bool send(const uint8_t* data, size_t size, int64_t pts_us) noexcept = 0;
virtual void shutdown() noexcept                                             = 0;
```
(`init`/`is_connected`'a dokunma — bunlar zaten hata durumunda `false` dönüyor,
shutdown/send kadar "sıcak yol" değiller ama tutarlılık için istersen onları da
ekleyebilirsin, zorunlu değil, SESSION_NOTES'a kararını yaz.)

### 2. `srt_transport.h`/`.cpp`

```cpp
bool send(const uint8_t* data, size_t size, int64_t pts_us) noexcept override;
void shutdown() noexcept override;
```
Gövdede `impl_.send_packet(...)`/`impl_.shutdown()` zaten exception fırlatmıyor
olabilir (SRT SDK'sı C tarzı, `bool` dönüyor) — ama garanti için gövdeyi
`try { ... } catch (...) { return false; }` (send) / `try { ... } catch (...) {}`
(shutdown) ile sar. Bu, "SRT SDK'sının içinde gizli bir exception yolu var mı"
sorusunu araştırmadan da güvenli hale getirir.

### 3. `rtmp_transport.h`/`.cpp`

Aynı şekilde `noexcept` + `try/catch` sarmalayıcı. **Özellikle önemli** —
Zig tarafı zaten panik=abort (C++'a hiç unwind etmiyor, bu değişmiyor), ama
`rtmp_transport.cpp`'nin KENDİSİ (C++ sarmalayıcı, `handle_` yönetimi, olası
`std::string`/`std::vector` işlemleri varsa) exception fırlatabilir — bunu
`try/catch` ile kapat.

### 4. Çağıran taraflar (`output_subsystem.cpp`, `pipeline.cpp`)

Değişiklik gerekmiyor — `noexcept` bir fonksiyonu çağırmak zaten güvenli,
imza uyumluluğu otomatik. Sadece derlemenin temiz geçtiğini doğrula (bazı
derleyiciler `noexcept` fonksiyonu `override` ederken taban sınıfın
`noexcept` olmamasını/olmasını uyumsuzlukla flagler — burada ikisi de
`noexcept` olacağı için sorun olmamalı, ama derle ve gör).

## Test

- Faz2/Aşama1'deki throw deneyini TEKRARLA — ama bu sefer beklenen sonuç
  FARKLI: `SrtTransport::shutdown()` içine geçici `throw` koy, şimdi
  `noexcept` sayesinde `std::terminate()` çağrılmalı (uygulama çöker, ama
  **kontrollü ve öngörülebilir** şekilde — SEH'in "belki yakalar belki
  yakalamaz" belirsizliği yerine kesin bir davranış). Bunu SESSION_NOTES'a
  "önce SEH yakalıyordu (belirsiz garanti), şimdi terminate ile kesin
  başarısız oluyor (net garanti)" diye karşılaştırmalı yaz.
- `OutputSubsystemTest`'in mevcut 7 testinin (SRT+RTMP simetrik) hâlâ PASS
  olduğunu doğrula — normal yol değişmemeli.
- `cmake --build --preset release` + `ctest --test-dir build` — bilinen 2
  hariç yeni kırılma olmamalı.

## Doğrulama Checklist

- [ ] `ITransport::send`/`shutdown` `noexcept`
- [ ] `SrtTransport`, `RtmpTransport` implementasyonları `noexcept` + iç `try/catch`
- [ ] `cmake --build --preset release` temiz
- [ ] Throw deneyi TEKRARLANDI, yeni davranış (terminate, SEH değil) doğrulandı ve SESSION_NOTES'a karşılaştırmalı yazıldı
- [ ] `OutputSubsystemTest` 7 test hâlâ PASS
- [ ] `ctest --test-dir build` — bilinen 2 hariç yeni kırılma yok
- [ ] `docs/FABLE5_BUG_PLAN_V8.md`'ye bu maddeyi yeni bir satır olarak ekle
      (I27 gibi bir numara ver, kaynağı "06.07.2026 taze tarama, Fable5+Opus4.8"
      olarak not et) VE `[DÜZELTILDI]` işaretle
- [ ] `.claude/skills/ffi-safety-review/SKILL.md`'ye kısa bir not: "C++ arayüz
      sınırları (ITransport gibi) SEH'e güvenmek yerine `noexcept` ile
      sağlamlaştırılmalı, özellikle çoklu implementasyon bekleniyorsa"
- [ ] Commit: `refactor(pipeline): ITransport'u noexcept ile sağlamlaştır (SEH virtual-call riski, Fable5+Opus4.8 06.07 taraması)`
- [ ] Push yapma — özet + test çıktısını raporla, onay bekle

## Sınır

Bu talimat `pipeline.cpp`/`output_subsystem.cpp`'deki dış SEH sarmalayıcıları
KALDIRMIYOR — Opus'un "SEH'i leaf implementasyonlara it" önerisi daha büyük bir
mimari değişiklik, ayrı bir karar olarak bırakılıyor. Bu talimat sadece
`noexcept` ekleyerek mevcut SEH + yeni noexcept'i birlikte, çelişkisiz çalışır
hale getiriyor (ikisi çakışmıyor — noexcept ihlali SEH'ten önce terminate'e gider).
