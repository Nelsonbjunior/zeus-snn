# Cérebro do Zeus: rosto + rede neural em ação

O Zeus tem uma rede neural de disparos (SNN) que roda no próprio ESP32, em paralelo com o LLM. São 266 neurônios do tipo LIF e cerca de 7.350 sinapses. A rede decide o humor e os gestos do rosto. O LLM continua cuidando da conversa, e cada humor que ele pede também ensina a rede. O OLED do robô mostra só o rosto; esta janela mostra o rosto e a rede funcionando.

## Abrir

Rode `build.bat` (precisa do Visual Studio 2022 com C++ e do .NET Framework 4.x que vem no Windows). Depois abra `bin\ZeusCerebro.exe`.

Para abrir já com a demonstração rodando: `bin\ZeusCerebro.exe --demo`.

## Modos

- **SIMULAÇÃO**: roda aqui no PC o mesmo código do firmware (`zeus_face.c`, `zeus_snn.c`, `zeus_cerebro.c`, compilados em `zeus_cerebro.dll`). O que o Zeus simulado aprende fica salvo em `%APPDATA%\ZeusCerebro.mem`. Apague esse arquivo para ele "nascer" de novo.
- **ROBÔ AO VIVO**: lê o cérebro de verdade do ESP32 pelo WiFi (`/snn`, `/snn/pesos` e `/frame`). Os botões mandam os estímulos para o robô. Digite o IP do robô no campo de cima.

## Painéis

| Painel | O que mostra |
|---|---|
| ZEUS.exe | o OLED (amarelo em cima, azul embaixo), igual ao do robô |
| SISTEMA NERVOSO | os 266 neurônios e as sinapses: sentidos à esquerda, ocultos no meio, emoções e gestos à direita, dopamina em cima. Neurônio que dispara acende, e as sinapses dele brilham |
| DISPAROS | cada ponto é um disparo nos últimos 2 s, uma linha por neurônio |
| NEURÔNIO #n | potencial de membrana subindo até o limiar, o disparo e a volta ao repouso. Clique num neurônio da rede para vigiá-lo |
| EMOÇÕES AGORA | humor, jeito, gesto, energia e a força de cada emoção |
| PERSONALIDADE | as tendências que se formam com o tempo: quem recebe muito carinho fica ALEGRE, quem leva bronca fica RANZINZA... |
| ESTÍMULOS | os 14 sentidos e os botões (carinho, bronca, cutucão, falar com ele, LLM pensando, xadrez...) |
| DOPAMINA | o nível de dopamina (recompensa e punição), o peso médio das sinapses que aprendem e os avisos "+1 RECOMPENSA", "-1 BRONCA" e "LLM ENSINOU" |

**VELOC.** acelera a simulação (×8, ×32) para ver o tédio e o sono chegarem. **DEMO** roda uma conversa roteirizada.

## Como a rede funciona

- **Neurônios LIF**: somam os sinais elétricos que chegam, vazam devagar e disparam ao passar do limiar. Depois de disparar voltam ao repouso, e o limiar sobe um pouco (adaptação).
- **Populações**:
  - 56 sensoriais (4 por sentido);
  - 128 ocultos excitatórios, agrupados em assembleias por sentido;
  - 32 inibitórios;
  - 28 de emoção (7 emoções);
  - 18 motores (6 gestos);
  - 4 de dopamina.
- **Aprendizado por reforço**: STDP modulada por dopamina, com traço de elegibilidade de cerca de 1 s. O carinho libera dopamina e fortalece as conexões que acabaram de levar a uma emoção; a bronca derruba a dopamina. Uma normalização faz as sinapses competirem, e o piso "de nascença" impede que ele desaprenda as reações básicas.
- **Em paralelo com o LLM**:
  - a ponte transforma a fala em estímulo (som; carinho ou bronca pelas palavras);
  - a rede sente "trabalho" enquanto o LLM pensa;
  - os sentimentos da rede vão junto para o LLM, que responde no tom de como o Zeus está.
- **Memória**: o robô grava na flash o que aprendeu (pesos e personalidade, cerca de 1,6 KB) no máximo a cada 5 min.

O banco de testes da rede fica em `sim\snn_teste.bat`: reações de nascença, professor (LLM), aprendizado, memória e 2 horas de uso.
