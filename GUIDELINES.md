# GUIDELINES.md — Reji Studio AI Çalışma Kuralları

> Her oturumda hatırlatıcı olarak kullanılır.
> AI ajanı bu dosyayı görev almadan önce okur.

---

## Altın Kurallar

### 1. Düşünme İşini Sen Yönet
- Strateji ve mimari kararlar sana ait
- AI kod yazar, sen yönlendirir ve onaylarsın
- "Bunu nasıl yapayım?" değil, "Bunu şöyle yap" de

### 2. Görevleri Küçük Parçalara Böl
```
❌ "Preview pipeline'ı düzelt ve NVENC entegre et"
✅ "Adım 1: preview_widget.cpp'de DwmFlush ekle. Tamamlanınca haber ver."
✅ "Adım 2: 30s stability test çalıştır. Crash yoksa devam."
✅ "Adım 3: Commit et."
```

### 3. Yapılmayacakları Belirt
Her görevde şunu ekle:
```
⛔ build/ dizinini silme
⛔ ffi_bridge.h dosyasına dokunma
⛔ CMakeLists.txt'i değiştirme (sadece belirtilen satır hariç)
⛔ PowerShell veya Git Bash ile build yapma — x64 Native Tools kullan
```

### 4. Çıktıları Doğrula
AI kodu yazdıktan sonra mutlaka:
```cmd
# Build et
cmake --build build --target reji_app

# Çalıştır
reji_app.exe > run.log 2>&1

# Log kontrol
findstr "HATA" run.log
findstr "First frame" run.log
```

### 5. Bağlamı Temiz Tut
- Her büyük görev sonrası `/clear`
- Yeni pencerede: AGENTS.md → CONTEXT.md → görev ver
- Aynı pencerede 10+ mesaj varsa yeni pencere aç

---

## Hata Ayıklama Protokolü

AI'ya hata verirken şunları ekle:

```
1. Hata mesajının tamamı (kısaltma)
2. Hangi komut çalıştırıldı
3. Hangi dosya değiştirildi
4. Önceki çalışan duruma göre ne değişti
```

**Kötü:**
```
Build hata veriyor
```

**İyi:**
```
cmake --build build --target reji_app çalıştırınca:
error LNK2019: çözümlenmemiş dış sembol SrtOutput::SrtOutput
Son değişiklik: srt_output_stub.cpp oluşturuldu
```

---

## Güvenli Kod Yazma

Her büyük feature sonrası:
```
/security-review src/pipeline/
```

Haftalık otomatik:
- `cargo audit` — GitHub Actions Pazartesi 09:00 UTC
- `cppcheck` — Her push

---

## Commit Kuralları

```
feat: yeni özellik
fix: bug düzeltme
fix(security): güvenlik açığı
docs: dokümantasyon
refactor: yeniden yapılandırma
test: test ekleme
chore: araç/config değişikliği
```

Her commit sonrası:
- `docs/progress.md` güncelle
- `CONTEXT.md` güncelle (büyük değişikliklerde)

---

## Yasaklar

```
❌ AI'ya "her şeyi düzelt" deme
❌ Uzun bağlamda mimari karar verme
❌ Test etmeden commit etme
❌ build/ dizinini CLI ile silme
❌ AI'nın build ortamını kurmasına izin verme (x64 Native Tools'u sen çalıştır)
❌ "allow all edits" — her değişikliği tek tek onayla
```

---

## Checklist — Görev Başlamadan Önce

- [ ] Model seçildi mi? (Haiku/Sonnet)
- [ ] Bağlam temiz mi? (yeni pencere açıldı mı?)
- [ ] AGENTS.md okundu mu?
- [ ] Görev küçük parçalara bölündü mü?
- [ ] Yapılmayacaklar belirtildi mi?
- [ ] Test planı hazır mı?
