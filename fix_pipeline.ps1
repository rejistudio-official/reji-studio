$f = 'C:\reji-studio\src\pipeline\pipeline.cpp'
$content = Get-Content $f -Raw -Encoding UTF8

# Edit 1: NVENC init failure — preview-only mode
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

# Edit 2: run_frame null encoder guard
$old2 = '            if (!s.encoder || !s.encoder->encode_frame(tex, pts_us)) {'
$new2 = '            if (s.encoder && !s.encoder->encode_frame(tex, pts_us)) {'

$result = $content.Replace($old1, $new1)
if ($result -eq $content) { Write-Host 'EDIT1: NO MATCH'; exit 1 }
Write-Host 'EDIT1: OK'

$result2 = $result.Replace($old2, $new2)
if ($result2 -eq $result) { Write-Host 'EDIT2: NO MATCH'; exit 1 }
Write-Host 'EDIT2: OK'

Set-Content -Path $f -Value $result2 -NoNewline -Encoding UTF8
Write-Host 'DONE'
