param(
    [string]$ProjectRoot = "C:\reji-studio"
)

function Write-OK   { param($m) Write-Host "  OK  $m" -ForegroundColor Green }
function Write-Fail { param($m) Write-Host "  ERR $m" -ForegroundColor Red }
function Write-Info { param($m) Write-Host "  ... $m" -ForegroundColor Cyan }

Write-Host ""
Write-Host "  Reji Studio - ABI Buyukluk Kontrolu" -ForegroundColor Magenta
Write-Host ""

# ---------------------------------------------------------------------------
# MSVC bul
# ---------------------------------------------------------------------------
function Find-Vcvars {
    $vswhere_paths = @(
        "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe",
        "C:\Program Files\Microsoft Visual Studio\Installer\vswhere.exe"
    )
    foreach ($vswhere in $vswhere_paths) {
        if (Test-Path $vswhere) {
            $inst = & $vswhere -latest -property installationPath 2>$null
            if ($inst) {
                $v = "$inst\VC\Auxiliary\Build\vcvars64.bat"
                if (Test-Path $v) { return $v }
            }
        }
    }
    foreach ($year in @("2026","2025","2022","2019")) {
        foreach ($ed in @("Community","Professional","Enterprise","BuildTools")) {
            $v = "C:\Program Files\Microsoft Visual Studio\$year\$ed\VC\Auxiliary\Build\vcvars64.bat"
            if (Test-Path $v) { return $v }
        }
    }
    return $null
}

$vcvars = Find-Vcvars
if (-not $vcvars) {
    Write-Fail "vcvars64.bat bulunamadi -MSVC kurulu degil veya yolu taninamadi."
    exit 1
}
Write-Info "MSVC: $vcvars"

# ---------------------------------------------------------------------------
# sizeof_check.cpp derle ve calistir
# ---------------------------------------------------------------------------
$tmpDir  = "$env:TEMP\rj-abi-check"
New-Item -ItemType Directory -Force $tmpDir | Out-Null

$srcFile = "$ProjectRoot\src\ffi\sizeof_check.cpp"
$incDir  = "$ProjectRoot\src\ffi"
$exeFile = "$tmpDir\sizeof_check.exe"

Write-Info "sizeof_check.cpp derleniyor..."

$tmpBat = "$tmpDir\compile.bat"
$batLine1 = '@echo off'
$batLine2 = 'call "' + $vcvars + '" x64 > nul 2>&1'
$batLine3 = 'if errorlevel 1 exit /b 1'
$batLine4 = 'cl.exe /nologo /EHsc /std:c++17 /I"' + $incDir + '" "' + $srcFile + '" /Fe:"' + $exeFile + '" /Fo:"' + $tmpDir + '\\" 2>&1'
$batContent = ($batLine1, $batLine2, $batLine3, $batLine4) -join "`r`n"
[System.IO.File]::WriteAllText($tmpBat, $batContent, [System.Text.Encoding]::ASCII)
$compileOut = cmd /c $tmpBat
if ($LASTEXITCODE -ne 0) {
    Write-Fail "Derleme basarisiz:"
    $compileOut | ForEach-Object { Write-Host "    $_" }
    exit 1
}
Write-OK "sizeof_check.cpp derlendi"

Write-Info "sizeof_check.exe calistiriliyor..."
$cppOutput = & $exeFile
$cppOutput | ForEach-Object { Write-Host "    $_" -ForegroundColor DarkGray }

# Cikti satirlarini ayristir:
#   sizeof(RjMetricSample) = 56 bytes
#   magic_tail offset: 52
$cppSizes   = @{}
$cppOffsets = @{}
foreach ($line in $cppOutput) {
    if ($line -match 'sizeof\((\w+)\)\s*=\s*(\d+)') {
        $cppSizes[$Matches[1]] = [int]$Matches[2]
    }
    if ($line -match '^(\w+) offset:\s*(\d+)') {
        $cppOffsets[$Matches[1]] = [int]$Matches[2]
    }
}

# ---------------------------------------------------------------------------
# Rust testleri calistir
# ---------------------------------------------------------------------------
Write-Info "Rust testleri calistiriliyor..."
$cargoExit = 0
cargo test --manifest-path "$ProjectRoot\src\orchestrator\Cargo.toml" 2>&1 | ForEach-Object {
    if ($_ -match "FAILED|error\[") {
        Write-Host "    $_" -ForegroundColor Red
        $cargoExit = 1
    } else {
        Write-Host "    $_" -ForegroundColor DarkGray
    }
}
if ($cargoExit -ne 0 -or $LASTEXITCODE -ne 0) {
    Write-Fail "Rust testleri basarisiz"
    exit 1
}
Write-OK "Rust testleri gecti"

# ---------------------------------------------------------------------------
# Beklenen degerlerle karsilastir
# (Rust const_assert ve C++ static_assert ile eslesen referans degerleri)
# ---------------------------------------------------------------------------
Write-Info "ABI degerleri karsilastiriliyor..."

$checks = @(
    @{ Type = "RjMetricSample"; Expected = 56 },
    @{ Type = "RjAction";       Expected = 20 },
    @{ Type = "RjCommand";      Expected = 24 }
)
$offsetChecks = @(
    @{ Field = "magic_tail"; Expected = 52 }
)

$allOk = $true
foreach ($chk in $checks) {
    $t = $chk.Type
    $e = $chk.Expected
    if ($cppSizes.ContainsKey($t)) {
        $a = $cppSizes[$t]
        if ($a -eq $e) {
            Write-OK "sizeof($t) = $a"
        } else {
            Write-Fail "sizeof($t): beklenen $e, gelen $a -ABI uyumsuz!"
            $allOk = $false
        }
    } else {
        Write-Fail "sizeof($t) ciktida bulunamadi"
        $allOk = $false
    }
}
foreach ($chk in $offsetChecks) {
    $f = $chk.Field
    $e = $chk.Expected
    if ($cppOffsets.ContainsKey($f)) {
        $a = $cppOffsets[$f]
        if ($a -eq $e) {
            Write-OK "offsetof(magic_tail) = $a"
        } else {
            Write-Fail "offsetof($f): beklenen $e, gelen $a -ABI uyumsuz!"
            $allOk = $false
        }
    } else {
        Write-Fail "offset($f) ciktida bulunamadi"
        $allOk = $false
    }
}

Write-Host ""
if (-not $allOk) {
    Write-Fail "ABI kontrolu BASARISIZ -sizeof_check.cpp veya metrics.rs guncellenmeli."
    exit 1
}
Write-OK "Tum ABI kontrolleri gecti (C++ <-> Rust uyumlu)"
