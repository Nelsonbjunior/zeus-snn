# Grava o firmware do Zeus pelo cabo USB SEM parar a ponte (zeus.appdevtest.shop continua no ar).
# A ponte so larga a COM durante a gravacao (/usb/soltar) e pega de volta no fim.
# Se a gravacao falhar, o ESP32 e reiniciado no modo normal (nao fica preso no modo de gravacao).
param([string]$porta = 'COM6', [int]$baud = 460800)
$ErrorActionPreference = 'Stop'
$py = 'C:\Espressif\tools\python\v6.0.2\venv\Scripts\python.exe'
$build = Join-Path $PSScriptRoot 'build'
$cfg = Get-Content (Join-Path $PSScriptRoot '..\ponte\zeus_ponte.json') -Raw | ConvertFrom-Json
$cab = @{ 'X-Zeus-Token' = $cfg.token }

function Ponte([int]$s) {
    try {
        $r = Invoke-RestMethod -Uri "http://127.0.0.1:8770/usb/soltar?s=$s" -Headers $cab -TimeoutSec 8
        Write-Output "ponte: cabo USB $(if ($s) { 'solto' } else { 'de volta' }) (usb=$($r.usb))"
    } catch { Write-Output "ponte: nao respondeu ($($_.Exception.Message)); segue assim mesmo" }
}

Ponte 300
$ok = $false
try {
    Push-Location $build
    & $py -m esptool --chip esp32s3 -p $porta -b $baud --before default-reset --after hard-reset write-flash '@flash_args'
    $ok = $LASTEXITCODE -eq 0
    Pop-Location
} finally {
    if (-not $ok) {
        # gravacao falhou: tira o ESP32 do modo de gravacao (EN em pulso com IO0 solto = boot normal)
        & $py -c "import serial,time;s=serial.Serial();s.port='$porta';s.baudrate=115200;s.dtr=False;s.rts=False;s.open();s.rts=True;time.sleep(0.15);s.rts=False;s.close()"
        Write-Output 'gravacao FALHOU: ESP32 reiniciado no firmware que ja estava nele'
    }
    Ponte 0
}
if ($ok) { Write-Output 'firmware gravado' } else { exit 1 }
