---
name: session-handoff
description: Reji Studio'da çalışma oturumunu kapatma ve devir prosedürü — CONTEXT.md, docs/memory.md, docs/SESSION_NOTES.md ve docs/progress.md güncellemelerinin standart formatı. Oturum sonunda, kullanıcı "bugünlük bu kadar", "oturumu kapat", "devir notu", "kaldığımız yeri kaydet" dediğinde veya büyük bir iş kalemi (aşama/sprint/faz) tamamlandığında bu skill'i uygula. Yeni oturum BAŞLANGICINDA da bu dosyaların okunma sırası için kullanılır.
---

# Session Handoff — Reji Studio

Amaç: bir sonraki oturum (sen, başka bir geliştirici veya Claude) sıfır
arkeolojiyle işe başlayabilsin. Dört dosyanın her birinin ayrı rolü var —
aynı bilgiyi dörde kopyalama, doğru dosyaya doğru türde bilgi yaz.

## Dosya rolleri

| Dosya | Ne girer | Ne GİRMEZ |
|---|---|---|
| `CONTEXT.md` | Anlık durum: aktif faz/aşama, sıradaki somut adım, bilinen engeller | Tarihçe, tamamlanmış işlerin dökümü |
| `docs/SESSION_NOTES.md` | Oturum günlüğü: bu oturumda ne yapıldı, ne açık kaldı | Kalıcı mimari karar (memory.md'ye gider) |
| `docs/memory.md` | Kalıcı bilgi: mimari kararlar, öğrenilen dersler, "bir daha yapma" listesi | Geçici durum, günlük ilerleme |
| `docs/progress.md` | Faz/aşama düzeyinde ilerleme işaretleri | Ayrıntılı anlatım |

Karar testi: *"Bu bilgi 3 ay sonra da doğru mu?"* Evet → memory.md.
*"Sadece şu an doğru mu?"* → CONTEXT.md. *"Bu oturuma mı özgü?"* → SESSION_NOTES.md.

## Oturum kapatma prosedürü

1. **Çalışır durumda bırak:** yarım değişiklik varsa ya tamamla ya stash'le —
   master'da kırık build bırakma. Son kontrol: `just build` + `just test` yeşil.
2. **repo-hygiene kontrolü:** `git status --porcelain` temiz mi?
   (çöp dosya kuralları için repo-hygiene skill'i)
3. **SESSION_NOTES.md** — en üste yeni oturum bloğu (mevcut format korunur):
   ```markdown
   ## Oturum: <GG Ay YYYY>

   ### Tamamlananlar
   - <madde: ne yapıldı + hangi dosya/mekanizma> (mevcut girdilerdeki
     ayrıntı seviyesini koru: "hysteresis_ms kural dosyasından okunuyor" gibi
     davranış düzeyi, satır düzeyi değil)

   ### Açık Kalemler
   - <bilerek yapılmayan/ertelenen işler + neden>
   ```
4. **CONTEXT.md** — durumu GÜNCELLE (ekleme değil, düzeltme):
   aktif faz/aşama, "sıradaki adım" tek cümlelik ve eyleme dönük olmalı
   ("H5-H8 bloğuna başla: shared_handle_ CloseHandle" gibi).
5. **memory.md** — SADECE şu durumlarda dokunulur:
   - Yeni mimari karar alındı (gerekçesiyle)
   - Acı verici bir ders öğrenildi (semptom + kök neden + kural)
   - Donanım/platform gerçeği keşfedildi (E_ACCESSDENIED örneği gibi)
   Girdi formatı: kalın başlık + 2-4 satır; roman değil.
6. **progress.md** — aşama/sprint tamamlandıysa işaretle; günlük iş için dokunma.
7. **İlgili skill güncellemesi:** oturumda yeni hata sınıfı/desen öğrenildiyse
   ilgili skill'e (vulkan-interop-debug ilkeleri, build-troubleshoot tablosu,
   obs-ws-protocol kapsam listesi) satır ekle.
8. **Commit:** dokümantasyon güncellemeleri kod commit'inden AYRI:
   `docs: session handoff <tarih> — <tek satır özet>`

## Oturum açma prosedürü (yeni oturum başlarken)

Okuma sırası: `CLAUDE.md` → `CONTEXT.md` (sıradaki adım) →
`SESSION_NOTES.md` son bloğu (açık kalemler) → gerekiyorsa `memory.md`
ilgili bölümü. Aktif göreve göre ilgili skill'i yükle.
İlk iş: `git log --oneline -5` ile CONTEXT.md'nin gerçekle tutarlılığını
doğrula — tutarsızsa önce CONTEXT.md'yi düzelt, sonra işe başla.

## Kalite ölçütü

İyi bir devir notunun testi: bu dosyaları okuyan biri, sana tek soru
sormadan 10 dakika içinde anlamlı işe başlayabilmeli. Başlayamıyorsa
eksik olan neyse bir dahaki kapanışta o bölümü güçlendir.
