# ESP32 Auto Build & Version Bump (Production Version)

$versionFile = "VERSION"

# 1. Detect and Increment Version
if (Test-Path $versionFile) {
    $v = (Get-Content $versionFile).Trim()
    Write-Host "Current Version: $v" -ForegroundColor Gray
    if ($v -match '(\d+)\.(\d+)\.(\d+)') {
        $major = $Matches[1]
        $minor = $Matches[2]
        $patch = [int]$Matches[3] + 1
        $newV = "$major.$minor.$patch"
    } else {
        $newV = "1.0.0"
    }
} else {
    $newV = "1.0.0"
}

"v$newV" | Out-File -Encoding ascii $versionFile
Write-Host ">>> Version bumped to: v$newV <<<" -ForegroundColor Green

# 2. Auto Git Tag
try {
    git add $versionFile
    git commit -m "release: bump version to v$newV"
    git tag -a "v$newV" -m "Release v$newV"
    Write-Host ">>> Git tag v$newV created <<<" -ForegroundColor Cyan
} catch {
    Write-Host "Git operation skipped" -ForegroundColor Yellow
}

# 3. Run PlatformIO Build
Write-Host ">>> Starting PlatformIO Build... <<<" -ForegroundColor Yellow
pio run -e hover_ble

if ($lastExitCode -eq 0) {
    Write-Host "`n========================================" -ForegroundColor Green
    Write-Host "  Build Success! Version: v$newV" -ForegroundColor Green
    Write-Host "  Path: .pio\build\hover_ble\firmware.bin" -ForegroundColor Gray
    Write-Host "========================================`n" -ForegroundColor Green

    # 4. Config
    $developerKeyID = "3" 
    $developerKeyPath = "$HOME\.hoverboard\id_rsa"
    $fullUri = "https://wcart.wozer.cn/api/ota/upload-secure"

    Write-Host ">>> Preparing secure push... <<<" -ForegroundColor Yellow
    
    $binPath = ".pio\build\hover_ble\firmware.bin"
    $fileHash = (Get-FileHash $binPath -Algorithm SHA256).Hash.ToLower().Trim()
    $commitMsg = (git log -1 --pretty=%s).Trim()
    
    # Message for signature: version|target|description|hash
    $target = "esp32"
    $msg = "v$newV|$target|$commitMsg|$fileHash"
    $msgFile = [System.IO.Path]::GetTempFileName()
    $sigFile = [System.IO.Path]::GetTempFileName()
    
    # Use .NET method to write text WITHOUT trailing newline
    [System.IO.File]::WriteAllText($msgFile, $msg)
    
    # Sign
    openssl dgst -sha256 -sign $developerKeyPath -out $sigFile $msgFile
    $signature = [Convert]::ToBase64String([System.IO.File]::ReadAllBytes($sigFile))
    
    Write-Host ">>> Uploading to $fullUri ..." -ForegroundColor Yellow
    
    # Use curl.exe for compatibility with PS 5.1
    $curlArgs = @(
        "-X", "POST",
        "$fullUri",
        "-H", "X-Key-ID: $developerKeyID",
        "-H", "X-Signature: $signature",
        "-F", "version=v$newV",
        "-F", "target=$target",
        "-F", "description=$commitMsg",
        "-F", "file=@$binPath"
    )

    & curl.exe @curlArgs

    if ($lastExitCode -eq 0) {
        Write-Host "`n>>> Push Successful and Firmware Released! <<<" -ForegroundColor Green
    } else {
        Write-Host "`n>>> Push Failed! <<<" -ForegroundColor Red
    }

    Remove-Item $msgFile, $sigFile
} else {
    Write-Host "Build FAILED. Please check for errors above." -ForegroundColor Red
}
