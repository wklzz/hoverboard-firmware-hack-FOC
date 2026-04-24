# STM32 Auto Push for OTA Testing
$binPath = ".pio\build\VARIANT_USART_BL\firmware.bin"
$newV = "1.0.1"
$target = "stm32"
$description = "STM32 OTA Test Build"
$developerKeyID = "3"
$developerKeyPath = "$HOME\.hoverboard\id_rsa"
$fullUri = "https://wcart.wozer.cn/api/ota/upload-secure"

if (!(Test-Path $binPath)) {
    Write-Host "Binary not found at $binPath" -ForegroundColor Red
    exit 1
}

Write-Host ">>> Preparing secure push for $target v$newV <<<" -ForegroundColor Yellow

$fileHash = (Get-FileHash $binPath -Algorithm SHA256).Hash.ToLower().Trim()
$msg = "v$newV|$target|$description|$fileHash"
$msgFile = [System.IO.Path]::GetTempFileName()
$sigFile = [System.IO.Path]::GetTempFileName()

[System.IO.File]::WriteAllText($msgFile, $msg)

# Sign
openssl dgst -sha256 -sign $developerKeyPath -out $sigFile $msgFile
$signature = [Convert]::ToBase64String([System.IO.File]::ReadAllBytes($sigFile))

Write-Host ">>> Uploading to $fullUri ..." -ForegroundColor Yellow

$curlArgs = @(
    "-X", "POST",
    "$fullUri",
    "-H", "X-Key-ID: $developerKeyID",
    "-H", "X-Signature: $signature",
    "-F", "version=v$newV",
    "-F", "target=$target",
    "-F", "description=$description",
    "-F", "file=@$binPath"
)

& curl.exe @curlArgs

if ($lastExitCode -eq 0) {
    Write-Host "`n>>> Push Successful! STM32 Firmware v$newV is now live. <<<" -ForegroundColor Green
} else {
    Write-Host "`n>>> Push Failed! <<<" -ForegroundColor Red
}

Remove-Item $msgFile, $sigFile
