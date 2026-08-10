# weekly.ps1 - Haftalik saglik taramasi (~30 dk). Kaynak: docs/GELISTIRME_RITMI.md B3
# Yalniz OTOMATIK kontroller. Insan gerektiren adimlar en altta yorum olarak listeli.
$ErrorActionPreference = "Continue"
Set-Location "$PSScriptRoot\.."
$manifest = "src\orchestrator\Cargo.toml"
$warn = @()

Write-Host "=== [1/6] cargo audit + cargo outdated ===" -ForegroundColor Cyan
if (Get-Command cargo-audit -ErrorAction SilentlyContinue) {
    cargo audit --file src\orchestrator\Cargo.lock
    if ($LASTEXITCODE -ne 0) { $warn += "cargo audit uyari/hata verdi" }
} else {
    $warn += "cargo-audit kurulu degil (cargo install cargo-audit)"
}
if (Get-Command cargo-outdated -ErrorAction SilentlyContinue) {
    cargo outdated --manifest-path $manifest
} else {
    $warn += "cargo-outdated kurulu degil (cargo install cargo-outdated)"
}

Write-Host "=== [2/6] ABI ritueli: ffi_auto.h yeniden uretim ===" -ForegroundColor Cyan
# ffi_auto.h'i build.rs (cbindgen) uretir - sil, yeniden urettir, diff bos olmali.
Remove-Item src\ffi\ffi_auto.h -ErrorAction SilentlyContinue
cargo build --manifest-path $manifest | Out-Null
if (-not (Test-Path src\ffi\ffi_auto.h)) {
    $warn += "ffi_auto.h yeniden URETILEMEDI - build.rs kirik olabilir"
} else {
    $abiDiff = git diff --stat -- src/ffi/ffi_auto.h
    if ($abiDiff) { $warn += "ffi_auto.h diff BOS DEGIL - ABI drift! ($abiDiff)" }
    else { Write-Host "ffi_auto.h diff bos - ABI stabil" -ForegroundColor Green }
}

Write-Host "=== [3/6] DOKUNMA dosyalari ===" -ForegroundColor Cyan
foreach ($f in @("src/orchestrator/src/metrics.rs", "src/ffi/ffi_bridge.h")) {
    $dirty = git status --porcelain -- $f
    if ($dirty) { $warn += "DOKUNMA dosyasi yerel degisiklik iceriyor: $f" }
    Write-Host "$f -> son commit: $(git log -1 --format='%h %ad %s' --date=short -- $f)"
}

Write-Host "=== [4/6] baseline_metrics.txt ===" -ForegroundColor Cyan
# Dosya bilerek izleniyor; kontrol = son commit'te beklenmedik degisiklik var mi?
Write-Host "Son commit: $(git log -1 --format='%h %ad %s' --date=short -- tests/baseline_metrics.txt)"
if (git status --porcelain -- tests/baseline_metrics.txt) {
    $warn += "baseline_metrics.txt yerel degisiklik iceriyor - commit'lemeden once gerekce sor"
}

Write-Host "=== [5/6] Test sayisi kontrolu (ctest 26, cargo 145+5+37) ===" -ForegroundColor Cyan
$ctestN = [int]((ctest --test-dir build -N 2>$null | Select-String "Total Tests: (\d+)").Matches[0].Groups[1].Value)
if ($ctestN -ne 26) { $warn += "ctest sayisi sapti: $ctestN (beklenen 26)" }
else { Write-Host "ctest: 26 - tamam" -ForegroundColor Green }
$cargoOut = cargo test --manifest-path $manifest 2>&1 | Select-String "^running (\d+) tests" | ForEach-Object { [int]$_.Matches[0].Groups[1].Value } | Where-Object { $_ -gt 0 }
$expected = @(145, 5, 37)
if (($cargoOut -join ",") -ne ($expected -join ",")) {
    $warn += "cargo test suite sayilari sapti: $($cargoOut -join '+') (beklenen 145+5+37)"
} else { Write-Host "cargo: 145+5+37 - tamam" -ForegroundColor Green }

Write-Host "=== [6/6] Ozet ===" -ForegroundColor Cyan
if ($warn.Count -gt 0) {
    Write-Host "HAFTALIK TARAMA: $($warn.Count) uyari" -ForegroundColor Yellow
    $warn | ForEach-Object { Write-Host "  - $_" -ForegroundColor Yellow }
    exit 1
}
Write-Host "HAFTALIK TARAMA: temiz" -ForegroundColor Green

# --- Insan gerektiren haftalik kontroller (otomatiklestirilmedi, bilerek) ---
# [SendDiag] butce kontrolu: uygulamayi calistir, log'da tot < 16.7ms mi bak
#   (5a816e5 preview duzeltmesinin regresyona ugramadigi)
# kullanicida etiketli Todoist gorev sayisi artiyor mu?
#   (kod bitiyor ama dogrulanmiyorsa borc birikiyor - Todoist'ten elle bak)
