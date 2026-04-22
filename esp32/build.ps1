# ESP32 Auto Build & Version Bump (Windows PowerShell)

$versionFile = "VERSION"

# 1. Detect and Increment Version
if (Test-Path $versionFile) {
    $v = (Get-Content $versionFile).Trim()
    Write-Host "Current Version: $v" -ForegroundColor Gray
    
    # Match major.minor.patch
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

# 2. Update VERSION file
"v$newV" | Out-File -Encoding ascii $versionFile
Write-Host ">>> Version bumped to: v$newV <<<" -ForegroundColor Green

# 3. Auto Git Tag
try {
    git add $versionFile
    git commit -m "release: bump version to v$newV"
    git tag -a "v$newV" -m "Release v$newV"
    Write-Host ">>> Git tag v$newV created <<<" -ForegroundColor Cyan
} catch {
    Write-Host "Git operation skipped (Git not found or no changes)" -ForegroundColor Yellow
}

# 4. Run PlatformIO Build
Write-Host ">>> Starting build... <<<" -ForegroundColor Yellow
pio run -e hover_ble

if ($lastExitCode -eq 0) {
    Write-Host "`n========================================" -ForegroundColor Green
    Write-Host "  Build Success! Version: v$newV" -ForegroundColor Green
    Write-Host "  Path: .pio\build\hover_ble\firmware.bin" -ForegroundColor Gray
    Write-Host "========================================`n" -ForegroundColor Green
} else {
    Write-Host "Build FAILED. Please check for errors above." -ForegroundColor Red
}
