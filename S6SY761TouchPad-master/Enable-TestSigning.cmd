@echo off
setlocal
fltmc >nul 2>&1
if errorlevel 1 (
    echo Uruchom ten plik jako Administrator.
    pause
    exit /b 1
)

echo Wlaczanie TESTSIGNING...
bcdedit /set testsigning on
if errorlevel 1 (
    echo.
    echo Windows odrzucil zmiane BCD.
    echo Sprawdz Secure Boot oraz dokladny komunikat powyzej.
    pause
    exit /b 1
)

echo.
echo TESTSIGNING wlaczony. Wymagany jest restart.
shutdown /r /t 0
