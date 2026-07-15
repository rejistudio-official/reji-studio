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

# Dinamik MSVC + Windows SDK yolu kesfet
$vsBase    = $vcvars -replace '\\VC\\Auxiliary\\Build\\vcvars64\.bat$', ''
$msvcBase  = "$vsBase\VC\Tools\MSVC"
$msvcVer   = (Get-ChildItem $msvcBase -ErrorAction SilentlyContinue |
              Sort-Object Name -Descending | Select-Object -First 1).Name
$sdkBase   = 'C:\Program Files (x86)\Windows Kits\10'
$sdkVer    = (Get-ChildItem "$sdkBase\Include" -ErrorAction SilentlyContinue |
              Sort-Object Name -Descending | Select-Object -First 1).Name

$msvcInc   = "$msvcBase\$msvcVer\include"
$msvcLib   = "$msvcBase\$msvcVer\lib\x64"
$ucrtInc   = "$sdkBase\Include\$sdkVer\ucrt"
$umInc     = "$sdkBase\Include\$sdkVer\um"
$sharedInc = "$sdkBase\Include\$sdkVer\shared"
$ucrtLib   = "$sdkBase\Lib\$sdkVer\ucrt\x64"
$umLib     = "$sdkBase\Lib\$sdkVer\um\x64"

Write-Info "MSVC $msvcVer  SDK $sdkVer"
Write-Info "sizeof_check.cpp derleniyor..."

$tmpBat   = "$tmpDir\compile.bat"
$batLine1 = '@echo off'
$batLine2 = 'call "' + $vcvars + '" x64 > nul 2>&1'
$clFlags  = '/nologo /EHsc /std:c++17' +
            ' /I"' + $incDir    + '"' +
            ' /I"' + $msvcInc   + '"' +
            ' /I"' + $ucrtInc   + '"' +
            ' /I"' + $umInc     + '"' +
            ' /I"' + $sharedInc + '"'
$clLink   = '/link /LIBPATH:"' + $msvcLib + '" /LIBPATH:"' + $ucrtLib + '" /LIBPATH:"' + $umLib + '"'
$batLine3 = 'cl.exe ' + $clFlags + ' "' + $srcFile + '" /Fe:"' + $exeFile + '" /Fo:"' + $tmpDir + '\\" ' + $clLink + ' 2>&1'
$batContent = ($batLine1, $batLine2, $batLine3) -join "`r`n"
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
#   sizeof(RjMetricSample) = 64 bytes
#   magic_tail offset: 56
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
    @{ Type = "RjMetricSample"; Expected = 64 },
    @{ Type = "RjAction";       Expected = 20 },
    @{ Type = "RjActionEvent";  Expected = 40 },
    @{ Type = "RjCommand";      Expected = 24 }
)
$offsetChecks = @(
    @{ Field = "magic_tail"; Expected = 56 }
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
