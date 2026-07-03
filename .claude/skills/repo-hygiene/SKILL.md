---
name: repo-hygiene
description: Reji Studio'da commit öncesi repo temizliği ve dosya disiplini. HER commit/push hazırlığında, `git add` kullanılan her görevde, yeni dosya oluştururken ve "commit'le", "push'la", "kaydet" istendiğinde bu skill'i uygula. Geçmişte yaşanan kazaları (redirect çıktısı dosyalar, .tmp dosyaları, dev log dosyaları commit'lenmesi) önlemek için tasarlandı. Kullanıcı "temizlik", "gitignore", "çöp dosya" veya "hygiene" dediğinde de tetiklenir.
---

# Repo Hygiene — Reji Studio

Geçmiş kazalar: `device())`, `selectRenderPath(pipeline_.display_vendor_id())`
gibi shell redirect kazası dosyaları; `debug_output.txt` (222KB),
`test_output.txt`, `repo_snapshot.txt`, `build_log.txt`;
`main_window.cpp.tmp3`, `pipeline.cpp.tmp2` geçici dosyaları commit'lendi.
Bu skill'in amacı: **bir daha asla.**

## Commit öncesi zorunlu kontrol

Her commit'ten önce (istisnasız):

```bash
git status --porcelain
```

Çıktıdaki HER dosyayı tek tek gerekçelendir. Şu desenlerden biri varsa
**stage etme, sil veya .gitignore'a ekle:**

| Desen | Tanı |
|---|---|
| `*.tmp*`, `*.bak`, `*~` | Editör/araç geçici dosyası |
| `*_output.txt`, `*_log.txt`, `*.log`, `run.log` | Çalışma/derleme çıktısı |
| `repo_snapshot*`, `ts.txt`, `nul` | Araç/script artığı |
| Adında `(`, `)`, boşluk olan dosya | Shell redirect kazası — komut satırı yanlış tırnaklanmış |
| `build/`, `target/` altı herhangi bir şey | Build artefaktı |
| `fable5-review-*.md`, `build_diag*` | Tarama/tanı çıktısı — docs/ altına damıtılmış plan gider, ham çıktı gitmez |
| `.reji/`, `rules.json` (kullanıcı config) | Makineye özgü — template'i `docs/config/` altında zaten var |

## `git add` disiplini

- **`git add .` ve `git add -A` YASAK.** Dosyaları isimle ekle:
  `git add src/pipeline/copy_optimizer.cpp src/pipeline/copy_optimizer.h`
- Stage sonrası son kontrol: `git diff --cached --stat` — beklemediğin
  dosya varsa `git restore --staged <dosya>`.
- Commit'e girecek dosya sayısı görevin kapsamıyla orantısızsa dur ve sor.

## Yeni dosya oluşturma kuralları (Claude için de geçerli)

- Geçici çalışma dosyası gerekiyorsa `build/` altında oluştur (zaten ignore'lu)
  veya işi bitince sil — repo kökünde `.tmp` bırakma.
- Shell'de çıktı yönlendirirken dosya adını tırnakla ve repo dışına yaz;
  fonksiyon çağrısı içeren satırları redirect hedefi yapma (geçmiş kazanın sebebi).
- Ham log/tarama çıktısı üretildiyse: değerli bilgi ilgili skill'e veya
  `docs/` altındaki plana damıtılır, ham dosya silinir.

## .gitignore bakımı

Yeni bir çöp deseni yakalandığında iki iş birden yapılır:
1. `.gitignore`'a desen eklenir (dar tut: `*.txt` gibi geniş desen yerine
   `*_output.txt` gibi hedefli desen; `CMakeLists.txt` benzeri meşru
   dosyaları yakalamasın).
2. Zaten izlenmeye başlamışsa: `git rm --cached <dosya>` (disk kopyası kalır).

## Tarihe sızmış dosya (ileri seviye)

Büyük/hassas dosya geçmişe girdiyse ve repo henüz yaygın fork'lanmadıysa:
`git filter-repo --invert-paths --path <dosya>` + force push. Bu yıkıcı bir
işlemdir — kullanıcı açıkça onaylamadan ASLA çalıştırma; sadece komutu öner.

## Otomasyon (öneri, kuruluysa uy)

`pre-commit` framework'ü kuruluysa hook'lar bu kuralların makine hali:
yasak desen kontrolü + büyük dosya limiti (500KB) + `just abi-check`
(FFI dosyaları değiştiyse). Hook başarısızsa `--no-verify` ile ATLAMA;
sebebini çöz.
