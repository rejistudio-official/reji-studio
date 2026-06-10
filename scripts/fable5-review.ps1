param(
    [string]$Module = "all",
    [switch]$DryRun = $false,
    [string]$OutputDir = "C:\reji-studio\docs\reviews",
    [string]$ProjectRoot = "C:\reji-studio\src"
)

function Write-Info  { param($msg) Write-Host "  $msg" -ForegroundColor Cyan }
function Write-OK    { param($msg) Write-Host "  OK  $msg" -ForegroundColor Green }
function Write-Warn  { param($msg) Write-Host "  !!  $msg" -ForegroundColor Yellow }
function Write-Fail  { param($msg) Write-Host "  ERR $msg" -ForegroundColor Red }

if (-not $DryRun -and -not $env:OPENROUTER_API_KEY) {
    Write-Fail "OPENROUTER_API_KEY bulunamadi."
    exit 1
}

$includePatterns = switch ($Module) {
    "pipeline"     { @("*.cpp","*.h") }
    "ui"           { @("*.cpp","*.h") }
    "ffi"          { @("*.h","*.c") }
    "orchestrator" { @("*.rs") }
    default        { @("*.cpp","*.h","*.rs","*.c") }
}

$includeDirs = switch ($Module) {
    "pipeline"     { @("$ProjectRoot\pipeline") }
    "ui"           { @("$ProjectRoot\ui") }
    "ffi"          { @("$ProjectRoot\ffi") }
    "orchestrator" { @("$ProjectRoot\orchestrator") }
    default        { @($ProjectRoot) }
}

Write-Host ""
Write-Host "  Reji Studio - Fable 5 Kod Tarama" -ForegroundColor Magenta
Write-Host "  Modul: $Module | DryRun: $DryRun" -ForegroundColor DarkGray
Write-Host ""

$allFiles = @()
foreach ($dir in $includeDirs) {
    if (Test-Path $dir) {
        foreach ($pattern in $includePatterns) {
            $found = Get-ChildItem $dir -Recurse -Filter $pattern -ErrorAction SilentlyContinue |
                Where-Object {
                    $_.FullName -notlike "*\build\*" -and
                    $_.FullName -notlike "*\node_modules\*" -and
                    $_.FullName -notlike "*\.git\*" -and
                    $_.Length -lt 200KB
                }
            $allFiles += $found
        }
    }
}

$allFiles = $allFiles | Sort-Object FullName -Unique

if ($allFiles.Count -eq 0) {
    Write-Fail "Hic dosya bulunamadi: $ProjectRoot"
    exit 1
}

Write-Info "Bulunan dosyalar: $($allFiles.Count)"

$codeBase = ""
$totalChars = 0
$skipped = @()

foreach ($file in $allFiles) {
    $relativePath = $file.FullName.Replace($ProjectRoot, "src")
    $content = Get-Content $file.FullName -Raw -Encoding UTF8 -ErrorAction SilentlyContinue
    if ($null -eq $content) { $content = "" }
    if (($totalChars + $content.Length) -gt 800000) {
        $skipped += $relativePath
        continue
    }
    $codeBase += "`n`n// FILE: $relativePath`n$content"
    $totalChars += $content.Length
}

Write-Info "Toplam karakter: $totalChars (~$([math]::Round($totalChars/4)) token)"

if ($skipped.Count -gt 0) {
    Write-Warn "$($skipped.Count) dosya token limiti nedeniyle atlandi"
}

if ($DryRun) {
    Write-OK "DryRun - Dosya listesi:"
    $allFiles | ForEach-Object { Write-Host "    $($_.Name)" -ForegroundColor DarkGray }
    exit 0
}

$moduleContext = switch ($Module) {
    "pipeline"     { "DXGI capture, NVENC encode, WASAPI audio, Vulkan GPU pipeline" }
    "ui"           { "Qt6 QOpenGLWidget preview, GL interop, ExternalMemoryBridge" }
    "ffi"          { "C ABI koprusu, FFI guvenligi, RjFrameData struct" }
    "orchestrator" { "Rust/Tokio event bus, self-healing, crossbeam SPSC" }
    default        { "C++17/MSVC + Rust/Tokio + Qt6 + Vulkan 1.4 tam kod tabani" }
}

$prompt = "Sen Reji Studio uzman kod denetcisisin.`n`nPROJE: Reji Studio - Windows canli yayin yazilimi`nSTACK: C++17/MSVC + Rust/Tokio + Qt6 6.8.0 + Vulkan 1.4 + DXGI`nDONANIM: AMD Radeon 780M (iGPU) + NVIDIA RTX 4070 Laptop (dGPU)`nMODUL: $moduleContext`n`nAsagidaki kategorilerde Turkce rapor yaz:`n1. KRITIK HATALAR (UB, null ptr, race condition, Vulkan spec ihlali)`n2. BELLEK YONETIMI (RAII ihlali, hot-path allocation, Vulkan object lifetime)`n3. VULKAN/GL INTEROP (sync eksigi, image layout, external memory)`n4. PERFORMANS (hot-path copy, suboptimal sync, cift GPU sorunu)`n5. MIMARI SORUNLAR (katman ihlali, refactor firsati)`n6. GUVENLIK (SEH scope, COM lifetime, input validation)`n`nHer bulgu icin: dosya, satir, aciklama, cozum onerisi.`n`nKOD TABANI ($($allFiles.Count) dosya):`n$codeBase"

$requestBody = @{
    model    = "anthropic/claude-5-fable-20260609"
    messages = @(@{ role = "user"; content = $prompt })
    max_tokens = 16000
} | ConvertTo-Json -Depth 5 -Compress

$headers = @{
    "Authorization" = "Bearer $env:OPENROUTER_API_KEY"
    "Content-Type"  = "application/json"
    "X-Title"       = "Reji Studio Code Review"
}

Write-Info "Fable 5'e gonderiliyor... (1-3 dakika)"

try {
    $response = Invoke-RestMethod -Uri "https://openrouter.ai/api/v1/chat/completions" `
        -Method Post -Headers $headers -Body $requestBody -TimeoutSec 300

    $msg = $response.choices[0].message
    $reviewContent = if ($msg.content) {
        $msg.content
    } elseif ($msg.reasoning) {
        $msg.reasoning
    } else {
        "Yanit bos geldi - content: $($msg | ConvertTo-Json)"
    }
    $usedTokens    = $response.usage.total_tokens
    $cost          = $response.usage.cost

    if (-not (Test-Path $OutputDir)) {
        New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
    }

    $timestamp  = Get-Date -Format "yyyy-MM-dd_HH-mm"
    $outputFile = "$OutputDir\fable5-review-$Module-$timestamp.md"

    $header = "# Fable 5 Kod Tarama Raporu`n**Tarih:** $(Get-Date -Format 'dd.MM.yyyy HH:mm')`n**Modul:** $Module`n**Dosya:** $($allFiles.Count)`n**Token:** $usedTokens | **Maliyet:** `$$cost`n`n---`n`n"
    ($header + $reviewContent) | Set-Content $outputFile -Encoding UTF8

    Write-OK "Tarama tamamlandi!"
    Write-Info "Rapor: $outputFile"
    Write-Info "Token: $usedTokens | Maliyet: `$$cost"

} catch {
    Write-Fail "API hatasi: $($_.Exception.Message)"
    exit 1
}
