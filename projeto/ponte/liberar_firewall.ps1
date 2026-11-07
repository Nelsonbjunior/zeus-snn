# Libera a porta da ponte do Zeus (8770) para o celular na rede local.
# Rode UMA vez, no PowerShell como administrador:
#   powershell -ExecutionPolicy Bypass -File D:\ROBO_Zeus\ponte\liberar_firewall.ps1
# So vale para redes marcadas como "Particular" no Windows (casa/escritorio).
$porta = 8770
$nome = "Zeus - ponte do notebook ($porta)"
if (Get-NetFirewallRule -DisplayName $nome -ErrorAction SilentlyContinue) {
    Write-Output "A regra '$nome' ja existe."
} else {
    New-NetFirewallRule -DisplayName $nome -Direction Inbound -Protocol TCP -LocalPort $porta `
        -Profile Private -RemoteAddress LocalSubnet -Action Allow | Out-Null
    Write-Output "Regra criada: '$nome' (rede Particular, so a sub-rede local)."
}
$perfil = (Get-NetConnectionProfile | Select-Object -First 1).NetworkCategory
Write-Output "Perfil da rede atual: $perfil"
if ($perfil -ne "Private") {
    Write-Output "ATENCAO: a rede nao esta como Particular; o celular nao vai alcancar a ponte."
}
