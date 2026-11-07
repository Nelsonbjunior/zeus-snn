# Liga o Hotspot Movel do Windows como a rede fixa do Zeus (2,4 GHz), compartilhando
# a internet que o PC estiver usando agora (WiFi 5 GHz, cabo, celular...).
# Uso: hotspot_pc.ps1 [-Nome ZEUS-PC] [-Senha ...] [-Status] [-Reiniciar]
# -Reiniciar: desliga e liga de novo (o PC trocou de rede: passa a compartilhar a nova)
param([string]$Nome = "ZEUS-PC", [string]$Senha = "", [switch]$Status, [switch]$Reiniciar)
$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Runtime.WindowsRuntime
$asTask = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
    $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and
    $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' })[0]
function Esperar($op, [type]$tipo) {
    $t = $asTask.MakeGenericMethod($tipo).Invoke($null, @($op))
    $t.Wait(-1) | Out-Null
    $t.Result
}

$null = [Windows.Networking.Connectivity.NetworkInformation, Windows.Networking.Connectivity, ContentType = WindowsRuntime]
$null = [Windows.Networking.NetworkOperators.NetworkOperatorTetheringManager, Windows.Networking.NetworkOperators, ContentType = WindowsRuntime]
$perfil = [Windows.Networking.Connectivity.NetworkInformation]::GetInternetConnectionProfile()
if (-not $perfil) { Write-Output "SEM_INTERNET"; exit 2 }
$tm = [Windows.Networking.NetworkOperators.NetworkOperatorTetheringManager]::CreateFromConnectionProfile($perfil)
$cfg = $tm.GetCurrentAccessPointConfiguration()

if ($Status) {
    $banda = try { $cfg.Band } catch { "?" }
    Write-Output ("estado={0} rede={1} banda={2} clientes={3} internet={4}" -f $tm.TetheringOperationalState, $cfg.Ssid, $banda, $tm.ClientCount, $perfil.ProfileName)
    exit 0
}

$mudou = $false
if ($cfg.Ssid -ne $Nome) { $cfg.Ssid = $Nome; $mudou = $true }
if ($Senha -and $cfg.Passphrase -ne $Senha) { $cfg.Passphrase = $Senha; $mudou = $true }
try {
    # 2,4 GHz: o ESP32 nao enxerga 5 GHz (Windows 11)
    $b24 = [Windows.Networking.NetworkOperators.TetheringWiFiBand]::TwoPointFourGigahertz
    if ($cfg.IsBandSupported($b24) -and $cfg.Band -ne $b24) { $cfg.Band = $b24; $mudou = $true }
    elseif (-not $cfg.IsBandSupported($b24)) { Write-Output "AVISO: este adaptador nao aceita 2,4 GHz agora" }
} catch { Write-Output "AVISO: Windows sem escolha de banda" }
try {
    # WPA2 puro: no "WPA3 transition mode" (padrao do Windows 11) o ESP32 ve a rede e nao entra
    $wpa2 = [Windows.Networking.NetworkOperators.TetheringWiFiAuthenticationKind]::Wpa2
    if ($cfg.IsAuthenticationKindSupported($wpa2) -and $cfg.AuthenticationKind -ne $wpa2) { $cfg.AuthenticationKind = $wpa2; $mudou = $true }
} catch { }
if ($mudou -or $Reiniciar) {
    if ($tm.TetheringOperationalState -eq "On") { Esperar ($tm.StopTetheringAsync()) ([Windows.Networking.NetworkOperators.NetworkOperatorTetheringOperationResult]) | Out-Null }
}
if ($mudou) {
    $acao = $tm.ConfigureAccessPointAsync($cfg)
    $asTaskAcao = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
        $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncAction' })[0]
    $asTaskAcao.Invoke($null, @($acao)).Wait(-1) | Out-Null
}
if ($tm.TetheringOperationalState -ne "On") {
    $r = Esperar ($tm.StartTetheringAsync()) ([Windows.Networking.NetworkOperators.NetworkOperatorTetheringOperationResult])
    if ($r.Status -ne "Success") { Write-Output ("ERRO {0} {1}" -f $r.Status, $r.AdditionalErrorMessage); exit 1 }
}
$cfg = $tm.GetCurrentAccessPointConfiguration()
Write-Output ("LIGADO rede={0} banda={1} internet={2}" -f $cfg.Ssid, $cfg.Band, $perfil.ProfileName)
