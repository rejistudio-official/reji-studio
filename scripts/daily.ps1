# daily.ps1 - Gunluk saglik taramasi (~5 dk). Kaynak: docs/GELISTIRME_RITMI.md B3
# Yalniz OTOMATIK kontroller - kod degistirmez, rapor basar.
$ErrorActionPreference = "Continue"
Set-Location "$PSScriptRoot\.."
$fail = 0

Write-Host "=== [1/4] ctest (26 beklenir) ===" -ForegroundColor Cyan
ctest --test-dir build --output-on-failure
if ($LASTEXITCODE -ne 0) { $fail++; Write-Host "ctest BASARISIZ" -ForegroundColor Red }

Write-Host "=== [2/4] cargo test ===" -ForegroundColor Cyan
cargo test --manifest-path src\orchestrator\Cargo.toml
if ($LASTEXITCODE -ne 0) { $fail++; Write-Host "cargo test BASARISIZ" -ForegroundColor Red }

Write-Host "=== [3/4] Son commit'ten bu yana degisiklik ===" -ForegroundColor Cyan
git diff --stat HEAD~1

Write-Host "=== [4/4] TODO/FIXME sayimi (izlenen kaynak dosyalar) ===" -ForegroundColor Cyan
$todoCount = (git grep -n -E "TODO|FIXME" -- src scripts 2>$null | Measure-Object -Line).Lines
Write-Host "TODO/FIXME: $todoCount adet"

if ($fail -gt 0) { Write-Host "`nGUNLUK TARAMA: $fail kontrol basarisiz" -ForegroundColor Red; exit 1 }
Write-Host "`nGUNLUK TARAMA: temiz" -ForegroundColor Green
