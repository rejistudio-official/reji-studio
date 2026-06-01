$f    = 'C:\reji-studio\src\pipeline\pipeline.cpp'
$tmp  = 'C:\reji-studio\src\pipeline\pipeline.cpp.tmp'

$content = Get-Content $f -Raw -Encoding UTF8

$old1 = @'
    if (!s.encoder->init(s.capture->encode_gpu()->d3d_device(),
                         enc_cfg, s.packet_cb)) {
        dbglog("[Pipeline] NvencEncoder::init failed");
        (void)shutdown(); return false;
    }
'@
$new1 = @'
    if (!s.encoder->init(s.capture->encode_gpu()->d3d_device(),
                         enc_cfg, s.packet_cb)) {
        dbglog("[Pipeline] NvencEncoder::init failed -- running in preview-only mode");
        s.encoder.reset();  // encode olmadan devam et
    }
'@

$old2 = '            if (!s.encoder || !s.encoder->encode_frame(tex, pts_us)) {'
$new2 = '            if (s.encoder && !s.encoder->encode_frame(tex, pts_us)) {'

$r = $content.Replace($old1, $new1)
if ($r -eq $content) { Write-Host 'EDIT1: NO MATCH'; exit 1 }
Write-Host 'EDIT1: OK'

$r2 = $r.Replace($old2, $new2)
if ($r2 -eq $r) { Write-Host 'EDIT2: NO MATCH'; exit 1 }
Write-Host 'EDIT2: OK'

# Write to temp, then replace
[System.IO.File]::WriteAllText($tmp, $r2, [System.Text.Encoding]::UTF8)
Move-Item -Path $tmp -Destination $f -Force
Write-Host 'DONE'
