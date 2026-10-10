param(
    [string]$Configuration = "Release",
    [string]$Platform = "ARM64"
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDir = Join-Path $Root "S6SY761Touch"
$Solution = Join-Path $Root "S6SY761Touch.sln"
$Stage = Join-Path $Root "TestPackage\$Platform"
$CertName = "S6SY761Touch Test"
$CerPath = Join-Path $Stage "S6SY761Touch-Test.cer"

function Find-Tool([string]$Name) {
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $kitsRoot = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\bin"
    if (Test-Path $kitsRoot) {
        $found = Get-ChildItem $kitsRoot -Recurse -File -Filter $Name -ErrorAction SilentlyContinue |
                 Sort-Object FullName -Descending |
                 Select-Object -First 1
        if ($found) { return $found.FullName }
    }

    throw "$Name nie znaleziono. Uruchom Developer Command Prompt/PowerShell z WDK."
}

function Assert-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $p = New-Object Security.Principal.WindowsPrincipal($id)
    if (-not $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw "Uruchom ten skrypt jako Administrator."
    }
}

Assert-Admin

$msbuild = Find-Tool "msbuild.exe"
$signtool = Find-Tool "signtool.exe"
$inf2cat  = Find-Tool "Inf2Cat.exe"

Write-Host "== Build ARM64 =="
& $msbuild $Solution "/p:Configuration=$Configuration" "/p:Platform=$Platform" "/m"
if ($LASTEXITCODE -ne 0) { throw "MSBuild zakończył się błędem: $LASTEXITCODE" }

$sys = Get-ChildItem $Root -Recurse -File -Filter "S6SY761Touch.sys" |
       Where-Object { $_.FullName -match "\\ARM64\\Release\\|\\ARM64\\" } |
       Sort-Object LastWriteTime -Descending |
       Select-Object -First 1

if (-not $sys) {
    throw "Nie znaleziono S6SY761Touch.sys po kompilacji ARM64."
}

Remove-Item $Stage -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $Stage -Force | Out-Null

Copy-Item $sys.FullName (Join-Path $Stage "S6SY761Touch.sys") -Force
Copy-Item (Join-Path $ProjectDir "S6SY761Touch.inf") (Join-Path $Stage "S6SY761Touch.inf") -Force

Write-Host "== Test certificate =="

$cert = Get-ChildItem Cert:\LocalMachine\My |
        Where-Object { $_.Subject -eq "CN=$CertName" -and $_.HasPrivateKey } |
        Sort-Object NotAfter -Descending |
        Select-Object -First 1

if (-not $cert) {
    $cert = New-SelfSignedCertificate `
        -Type CodeSigningCert `
        -Subject "CN=$CertName" `
        -CertStoreLocation "Cert:\LocalMachine\My" `
        -KeyAlgorithm RSA `
        -KeyLength 2048 `
        -HashAlgorithm SHA256 `
        -KeyExportPolicy NonExportable `
        -NotAfter (Get-Date).AddYears(5)
}

Export-Certificate -Cert $cert -FilePath $CerPath -Force | Out-Null

Write-Host "== Sign SYS =="
& $signtool sign /v /fd SHA256 /sm /s My /sha1 $cert.Thumbprint (Join-Path $Stage "S6SY761Touch.sys")
if ($LASTEXITCODE -ne 0) { throw "SignTool nie podpisał .sys: $LASTEXITCODE" }

$build = [int](Get-ItemPropertyValue "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion" "CurrentBuild")

if ($build -ge 26200) {
    $os = "10_25H2_ARM64"
} elseif ($build -ge 26100) {
    $os = "10_GE_ARM64"
} elseif ($build -ge 22621) {
    $os = "10_NI_ARM64"
} else {
    throw "Nieobsługiwany/nieznany build Windows ($build). Zatrzymuję skrypt."
}

Write-Host "== Generate CAT for $os =="
& $inf2cat /driver:$Stage /os:$os /uselocaltime /verbose
if ($LASTEXITCODE -ne 0) { throw "Inf2Cat zakończył się błędem: $LASTEXITCODE" }

$cat = Get-ChildItem $Stage -File -Filter "S6SY761Touch.cat" | Select-Object -First 1
if (-not $cat) { throw "Inf2Cat nie utworzył S6SY761Touch.cat." }

Write-Host "== Sign CAT =="
& $signtool sign /v /fd SHA256 /sm /s My /sha1 $cert.Thumbprint $cat.FullName
if ($LASTEXITCODE -ne 0) { throw "SignTool nie podpisał .cat: $LASTEXITCODE" }

Write-Host "== Trust test certificate locally =="
& certutil.exe -addstore -f Root $CerPath | Out-Host
& certutil.exe -addstore -f TrustedPublisher $CerPath | Out-Host

Write-Host ""
Write-Host "Gotowy pakiet:"
Write-Host $Stage
Write-Host ""
Write-Host "Następny krok: Enable-TestSigning.cmd -> restart -> Install-TestDriver.cmd"
