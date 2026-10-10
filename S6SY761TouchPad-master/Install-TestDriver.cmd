@echo off
setlocal
fltmc >nul 2>&1
if errorlevel 1 (
    echo Uruchom ten plik jako Administrator.
    pause
    exit /b 1
)

set "PKG=%~dp0TestPackage\ARM64\S6SY761Touch.inf"

if not exist "%PKG%" (
    echo Brak pakietu: %PKG%
    echo Najpierw uruchom Prepare-Test-Package.ps1
    pause
    exit /b 1
)

echo.
echo Instalowanie S6SY761Touch...
pnputil /add-driver "%PKG%" /install
set "ERR=%ERRORLEVEL%"

echo.
if not "%ERR%"=="0" (
    echo PnPUtil zakonczyl sie bledem: %ERR%
    echo Sprawdz setupapi.dev.log oraz Event Viewer - CodeIntegrity.
) else (
    echo Instalacja zakonczona.
)
pause
exit /b %ERR%
