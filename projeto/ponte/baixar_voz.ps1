# Baixa a voz pt-BR do Piper (faber, qualidade media, ~63 MB) para ponte\vozes.
# A voz roda offline no notebook; depois de baixada, nao precisa de internet para falar.
$ErrorActionPreference = 'Stop'
$pasta = Join-Path $PSScriptRoot 'vozes'
New-Item -ItemType Directory -Force $pasta | Out-Null
$base = 'https://huggingface.co/rhasspy/piper-voices/resolve/main/pt/pt_BR/faber/medium'
foreach ($arq in 'pt_BR-faber-medium.onnx', 'pt_BR-faber-medium.onnx.json') {
    $destino = Join-Path $pasta $arq
    if (Test-Path $destino) { Write-Output "ja existe: $arq"; continue }
    Write-Output "baixando $arq..."
    Invoke-WebRequest -Uri "$base/$arq" -OutFile $destino -UseBasicParsing
}
Write-Output 'voz pronta. Reinicie a ponte.'
