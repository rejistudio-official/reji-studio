# lint.ps1 - PostToolUse hook: Rust dosyasi duzenlendiginde cargo clippy kosar.
# Kayit: .claude/settings.json -> hooks.PostToolUse (matcher: Edit|Write).
# Girdi: stdin JSON, dosya yolu tool_input.file_path alaninda
#   (CLAUDE_FILE_PATHS diye bir ortam degiskeni YOK - resmi hook dokumani, 2026-08-10).
# Cikis: 0 = temiz veya ilgisiz dosya; 2 = clippy kirmizi (stderr Claude'a geri bildirilir).
# Kabuk uyumu tek yerde cozulsun diye tum mantik bu script'te; hook komutu yalniz cagirir.
# Olculen sure (2026-08-10, artimli): ~2.6 sn - 30 sn akis-bozma esiginin altinda.
$ErrorActionPreference = "Continue"

$raw = [Console]::In.ReadToEnd()
if (-not $raw) { exit 0 }
try { $evt = $raw | ConvertFrom-Json } catch { exit 0 }

$path = $evt.tool_input.file_path
if (-not $path -or $path -notmatch '\.rs$') { exit 0 }

$manifest = Join-Path (Split-Path $PSScriptRoot -Parent) "src\orchestrator\Cargo.toml"
$out = cargo clippy --manifest-path $manifest --all-targets -- -D warnings 2>&1
if ($LASTEXITCODE -ne 0) {
    $tail = ($out | ForEach-Object { "$_" } | Select-Object -Last 40) -join "`n"
    [Console]::Error.WriteLine("clippy -D warnings KIRMIZI (${path}):`n$tail")
    exit 2
}
exit 0
