@echo off
setlocal
fltmc >nul 2>&1
if errorlevel 1 (
    echo Uruchom ten plik jako Administrator.
    pause
    exit /b 1
)

echo Wylaczanie TESTSIGNING...
bcdedit /set testsigning off
if errorlevel 1 (
    echo Nie udalo sie wylaczyc TESTSIGNING.
    pause
    exit /b 1
)

echo.
echo TESTSIGNING wylaczony. Wymagany jest restart.
shutdown /r /t 0
