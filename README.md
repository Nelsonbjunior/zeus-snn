# Código-fonte do Zeus e da rede SNN

O diretório `projeto` contém os fontes e configurações do firmware ESP32, da ponte Python, do aplicativo Android, dos simuladores Windows, ferramentas e fontes da documenta??o. A SNN é escrita em C; a ponte usa Python e o simulador usa C#. Credenciais, memórias aprendidas e binários não fazem parte do pacote.

## Compilar a SNN no PC

No Windows x64 instale Visual Studio 2022 Community com desenvolvimento para desktop em C++ e .NET Framework 4.x. Os scripts esperam o Visual Studio em `C:\Program Files\Microsoft Visual Studio\2022\Community`; ajuste o caminho nos arquivos `.bat` se necessário.

Execute `projeto\sim\cerebro\build.bat` e abra `projeto\sim\cerebro\bin\ZeusCerebro.exe --demo`. O simulador executa no processador Intel ou AMD os mesmos fontes SNN do firmware, em modo local. `projeto\sim\snn_teste.bat` compila e executa o banco de testes. O firmware continua executando sua própria SNN no ESP32; o simulador local tem estado próprio.

## Compilar o firmware

Instale ESP-IDF 6.0.2 para ESP32-S3. Copie `projeto\firmware\main\secrets.example.h` para `secrets.h` e configure Wi-Fi e demais parâmetros. Em um terminal ESP-IDF, na pasta `projeto\firmware`:

```
idf.py set-target esp32s3
idf.py build
idf.py -p COM6 flash monitor
```

Troque COM6 pela porta da placa. Configure os endereços e tokens para o ambiente escolhido.

## Ponte e Android

A ponte requer Python 3 e as dependências em `projeto\ponte\requirements.txt`; instale-as com `py -m pip install -r requirements.txt` dentro da pasta ponte. Execute `py zeus_ponte.py`. Recursos opcionais exigem Ollama, Stockfish ou modelo de voz externo. A extensão do Instagram recebe sua configuração da ponte.

Para Android, abra `projeto\android` no Android Studio e instale Android SDK 35, JDK 17 e Gradle compatível com Android Gradle Plugin 8.9.2. O pacote não inclui Gradle Wrapper; configure Gradle no Android Studio ou gere o wrapper. A compilação inicial pode baixar dependências.

## Aumentar a rede em Intel ou AMD

Os 266 são neurônios, não sinapses. Na configuração original há aproximadamente 7.400 sinapses; o valor exato depende da montagem e da semente. Um PC pode simular redes maiores, conforme CPU, memória e topologia. A SNN roda em CPU e não se torna automaticamente paralela ou maior por estar no PC. Para tempo real, meça se 1.000 passos de 1 ms terminam em até um segundo no computador utilizado.

Para pesquisar uma rede maior, trabalhe numa cópia:

1. Em `firmware/main/zeus_snn.h`, revise `SNN_NEXC` e `SNN_NINH`; `SNN_N` e os offsets são derivados. Revise também `SNN_SIN_MAX`. Aumentar essa capacidade não cria sinapses por si só: a montagem e suas probabilidades estão em `zeus_snn.c`.
2. Em `zeus_snn.c`, revise `PL_MAX`, limite das sinapses plásticas. Quando `SNN_SIN_MAX` ou `PL_MAX` são atingidos, conexões adicionais deixam de ser criadas; conte as sinapses reais e verifique a saturação.
3. Índices, offsets, contagens e persistência usam `uint16_t` em vários pontos. Redes que excedam a faixa de 16 bits exigem ampliar tipos, cálculos, API e armazenamento, além de rever o protocolo HTTP e o visualizador.
4. Alterar tamanho, topologia ou semente pode invalidar memórias existentes ou associar pesos salvos a conexões diferentes. Preserve os arquivos originais e inicie uma memória nova, ou implemente migração e identificação da topologia.
5. Revise o visualizador, buffers e protocolos para as novas dimensões. Meça tempo, RAM, número de conexões, comportamento e aprendizado. Nenhum tamanho maior foi validado neste pacote.

Essa ampliação é uma proposta técnica. O código incluído mantém a configuração original.

