# Rosto do Zeus numa janela do Windows

Espelho ao vivo do OLED: a janela mostra os pixels reais que o firmware está desenhando, lidos pela rota `/frame` do ESP32. As 16 linhas de cima aparecem em amarelo e o resto em azul, como no SSD1306 de duas cores do robô. Animações, expressões, textos e o "calculando..." do xadrez aparecem sozinhos.

## Compilar e abrir

Rode `build.bat` (usa o compilador do .NET Framework 4.x que já vem no Windows). Depois abra `bin\app\ZeusFaceWindow.exe`.

## Como conecta

Com o campo **Robô** em `auto`, a janela acha o robô sozinha:

1. pergunta o IP à ponte (`http://127.0.0.1:8770/app/cfg`, com o token de `ponte\zeus_ponte.json`);
2. se a ponte não souber, testa os vizinhos da tabela ARP, começando pelo hotspot do PC (`192.168.137.x`).

Também dá para digitar o IP (por exemplo `192.168.137.4`) e apertar Enter. Se o robô parar de responder, a janela procura de novo sozinha. O endereço e a opção de grade ficam salvos em `%APPDATA%\ZeusOled.txt`.

## O que tem na janela

- **1 a 8**: expressões (também pelas teclas 1–8). O botão aceso acompanha o humor atual do robô, mesmo quando quem mudou foi o app do celular.
- **Exibir**: mostra o texto no OLED (Enter no campo também).
- **Falando**: mantém a boca mexendo.
- **Grade**: desenha o vão entre os pixels, como no OLED de verdade.
- **Tela cheia**: F11 ou duplo clique; Esc sai. Para usar num projetor, arraste a janela para essa tela antes.
- Rodapé: quadros por segundo, quantos quadros mudaram e o tempo de cada pedido.

Os comandos vão pela ponte, que entrega ao robô pelo canal da internet, WiFi ou cabo USB. Com a ponte desligada, vão direto ao robô.

## Desempenho

O ESP32 leva cerca de 80 ms para responder cada `/frame`. A janela mantém dois pedidos em paralelo numa conexão reaproveitada: assim chegam ~25 quadros/s, o mesmo ritmo em que o robô desenha o rosto (40 ms). Tudo é assíncrono (`async`/`await`), então a janela nunca trava esperando a rede. Cada quadro é copiado de uma vez para o bitmap, e a tela só é redesenhada quando o quadro muda.
