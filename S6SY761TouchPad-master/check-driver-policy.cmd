@echo off
echo === BCD: test signing / integrity ===
bcdedit /enum {current} | findstr /i "testsigning nointegritychecks debug"
echo.
echo === Secure Boot ===
powershell.exe -NoProfile -Command "try { Confirm-SecureBootUEFI } catch { Write-Host ('Secure Boot query unavailable: ' + $_.Exception.Message) }"
echo.
echo This script only reads configuration. It does NOT change BCD.
pause
