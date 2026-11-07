// Gera Zeus_Sinais_e_Discreta.pptx — apresentacao ludica (pt-BR).
// node gerar.js   (precisa de onda.json, exportado da voz real do Zeus)
const path = require("path");
const pptxgen = require("pptxgenjs");
const React = require("react");
const RDS = require("react-dom/server");
const sharp = require("sharp");
const fa = require("react-icons/fa");
const { applyTheme } = require(path.join(process.env.PPTX_SKILL, "scripts", "apply_theme.js"));
const onda = require("./onda.json"); // numeros reais, gerados por analisar.py
const nf = (v, d = 0) => v.toLocaleString("pt-BR", { minimumFractionDigits: d, maximumFractionDigits: d });
const DUR = onda.n / onda.sr;
const MARCA = (acum) => 0.05 + DUR * 0.95 * acum / 19; // voz.marcas_de_palavra para "Oi, tudo bem?"

const THEME = {
  name: "Zeus OLED",
  headFontFace: "Bookman Old Style",
  bodyFontFace: "Calibri",
  colors: {
    dk1: "1A1D2E", lt1: "FFFFFF", dk2: "14182E", lt2: "EEF1F8",
    accent1: "FFC93C", accent2: "2EC4D6", accent3: "FF6B6B",
    accent4: "7B61FF", accent5: "3DDC97", accent6: "8A93AB",
    hlink: "2EC4D6", folHlink: "7B61FF",
  },
};
const HEX = THEME.colors;

const pres = new pptxgen();
pres.layout = "LAYOUT_WIDE"; // 13.33 x 7.5
pres.title = "Como o Zeus ouve, pensa e fala";
pres.author = "Robô Zeus";
pres.theme = { headFontFace: THEME.headFontFace, bodyFontFace: THEME.bodyFontFace };
const C = pres.SchemeColor;
const W = 13.333;

async function icone(Comp, cor, px = 256) {
  const svg = RDS.renderToStaticMarkup(React.createElement(Comp, { color: "#" + cor, size: px }));
  const buf = await sharp(Buffer.from(svg)).resize(px, px).png().toBuffer();
  return "image/png;base64," + buf.toString("base64");
}

// ---------- layouts ----------
pres.defineSlideMaster({
  title: "CAPA",
  background: { color: C.text2 },
  objects: [
    { placeholder: { options: { name: "title", type: "title", x: 0.7, y: 1.6, w: 7.4, h: 2.4,
      fontSize: 44, bold: true, color: C.background1, valign: "bottom", align: "left" }, text: "" } },
    { placeholder: { options: { name: "body", type: "body", x: 0.7, y: 4.15, w: 7.2, h: 1.4,
      fontSize: 20, color: C.accent2, valign: "top" }, text: "" } },
  ],
});
pres.defineSlideMaster({
  title: "CONTEUDO",
  background: { color: C.background1 },
  margin: [0.5, 0.6, 0.6, 0.6],
  objects: [
    { placeholder: { options: { name: "title", type: "title", x: 0.6, y: 0.35, w: 12.1, h: 0.95,
      fontSize: 32, bold: true, color: C.text2, valign: "middle", align: "left" }, text: "" } },
  ],
  slideNumber: { x: 12.35, y: 6.95, w: 0.6, h: 0.35, fontSize: 11, color: C.accent6, align: "right" },
});
pres.defineSlideMaster({
  title: "SECAO",
  background: { color: C.text2 },
  objects: [
    { placeholder: { options: { name: "title", type: "title", x: 0.7, y: 2.5, w: 8.2, h: 1.6,
      fontSize: 44, bold: true, color: C.background1, valign: "bottom", align: "left" }, text: "" } },
    { placeholder: { options: { name: "body", type: "body", x: 0.7, y: 4.25, w: 8.0, h: 1.2,
      fontSize: 20, color: C.accent1, valign: "top" }, text: "" } },
  ],
  slideNumber: { x: 12.35, y: 6.95, w: 0.6, h: 0.35, fontSize: 11, color: C.accent6, align: "right" },
});

const txt = (s, t, o) => s.addText(t, Object.assign({ isTextBox: true }, o));
const sombra = () => ({ type: "outer", color: "000000", opacity: 0.18, blur: 8, offset: 3, angle: 90 });

// carinha do Zeus (o OLED de verdade: faixa amarela em cima, olhos ciano)
function rosto(s, x, y, w, faixa, nome) {
  const h = w / 2;
  s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x, y, w, h, rectRadius: 0.12, fill: { color: "000000" },
    line: { color: C.accent6, width: 1.5 }, objectName: nome + " tela" });
  s.addShape(pres.shapes.RECTANGLE, { x: x + 0.08, y: y + 0.08, w: w - 0.16, h: h * 0.22, fill: { color: C.accent1 },
    objectName: nome + " faixa" });
  txt(s, faixa, { x: x + 0.08, y: y + 0.08, w: w - 0.16, h: h * 0.22, fontSize: Math.max(10, w * 4.2), bold: true,
    color: "000000", align: "center", valign: "middle", margin: 0, fontFace: "Courier New", objectName: nome + " texto" });
  const ew = w * 0.2, eh = h * 0.42, ey = y + h * 0.4;
  for (const ex of [x + w * 0.24, x + w * 0.56]) {
    s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: ex, y: ey, w: ew, h: eh, rectRadius: 0.08,
      fill: { color: C.accent2 }, line: { color: C.accent2 }, objectName: nome + " olho" });
  }
}

function bolinhaIcone(s, img, x, y, d, cor, nome) {
  s.addShape(pres.shapes.OVAL, { x, y, w: d, h: d, fill: { color: cor }, line: { color: cor }, objectName: nome + " círculo" });
  s.addImage({ data: img, x: x + d * 0.25, y: y + d * 0.25, w: d * 0.5, h: d * 0.5, objectName: nome + " ícone" });
}

(async () => {
  const I = {
    mic: await icone(fa.FaMicrophone, "14182E"),
    texto: await icone(fa.FaCommentDots, "14182E"),
    cerebro: await icone(fa.FaBrain, "FFFFFF"),
    onda: await icone(fa.FaWaveSquare, "14182E"),
    som: await icone(fa.FaVolumeUp, "14182E"),
    cavalo: await icone(fa.FaChessKnight, "14182E"),
    cavaloG: await icone(fa.FaChessKnight, "FFC93C", 512),
    tesoura: await icone(fa.FaCut, "FF6B6B"),
    ouvido: await icone(fa.FaAssistiveListeningSystems, "14182E"),
    karaoke: await icone(fa.FaMicrophoneAlt, "14182E"),
    regua: await icone(fa.FaRulerVertical, "14182E"),
    equalizador: await icone(fa.FaSlidersH, "14182E"),
    copias: await icone(fa.FaClone, "14182E"),
    picadinho: await icone(fa.FaPuzzlePiece, "FFFFFF"),
    talheres: await icone(fa.FaUtensils, "14182E"),
    liquidificador: await icone(fa.FaBlender, "14182E"),
    colher: await icone(fa.FaUtensilSpoon, "14182E"),
    volume: await icone(fa.FaVolumeDown, "14182E"),
    barras: await icone(fa.FaChartBar, "14182E"),
    lupa: await icone(fa.FaSearch, "14182E"),
    chave: await icone(fa.FaKey, "14182E"),
    pegadas: await icone(fa.FaShoePrints, "14182E"),
    ventilador: await icone(fa.FaFan, "14182E"),
  };

  // ================= 1. capa =================
  pres.addSection({ title: "Abertura" });
  let s = pres.addSlide({ masterName: "CAPA", sectionTitle: "Abertura" });
  s.addText("Como o Zeus ouve, pensa e fala", { placeholder: "title" });
  s.addText("Processamento de sinais e matemática discreta dentro de um robô de verdade", { placeholder: "body" });
  rosto(s, 8.6, 2.2, 4.0, "Oi! Tudo bem?", "Zeus capa");
  txt(s, "ESP32-S3 · OLED · celular · notebook", { x: 8.6, y: 4.4, w: 4.0, h: 0.4, fontSize: 14,
    color: C.accent6, align: "center", objectName: "legenda capa" });
  s.addNotes("Abertura. Apresente o Zeus: um robozinho com ESP32-S3 e uma telinha OLED. Ele conversa por voz, " +
    "joga xadrez e mostra emoções. A pergunta da palestra: o que acontece entre a gente falar 'Zeus, oi' e ele responder? " +
    "Spoiler: um monte de números (sinais) e um monte de contas com coisas contáveis (matemática discreta).");

  // ================= 2. viagem da frase =================
  s = pres.addSlide({ masterName: "CONTEUDO", sectionTitle: "Abertura" });
  s.addText("A viagem de uma frase até o Zeus responder", { placeholder: "title" });
  const passos = [
    [I.mic, C.accent1, "Microfone", "o celular grava o ar vibrando", "SINAL"],
    [I.texto, C.accent2, "Reconhecimento", "som vira texto: “Zeus, oi”", "SINAL"],
    [I.cerebro, C.accent4, "Cérebro", "Llama 3.1 escolhe a resposta", "DISCRETA"],
    [I.onda, C.accent5, "Piper", "texto vira onda sonora", "SINAL"],
    [I.som, C.accent3, "Alto-falante + boca", "o notebook fala, o OLED mexe a boca", "SINAL"],
  ];
  const px0 = 0.75, pdx = 2.5, pd = 1.25;
  passos.forEach(([img, cor, tit, desc, tag], i) => {
    const x = px0 + i * pdx;
    bolinhaIcone(s, img, x + (2.1 - pd) / 2, 1.95, pd, cor, "passo " + (i + 1));
    txt(s, String(i + 1), { x: x + (2.1 - pd) / 2 - 0.1, y: 1.8, w: 0.45, h: 0.45, shape: pres.shapes.OVAL,
      fill: { color: C.text2 }, color: C.background1, fontSize: 14, bold: true, align: "center", valign: "middle",
      margin: 0, objectName: "número passo " + (i + 1) });
    txt(s, tit, { x, y: 3.4, w: 2.1, h: 0.5, fontSize: 18, bold: true, color: C.text2, align: "center", margin: 0,
      objectName: "título passo " + (i + 1) });
    txt(s, desc, { x, y: 3.9, w: 2.1, h: 0.85, fontSize: 14, color: C.text1, align: "center", valign: "top", margin: 0,
      objectName: "descrição passo " + (i + 1) });
    txt(s, tag, { x: x + 0.35, y: 4.95, w: 1.4, h: 0.4, shape: pres.shapes.ROUNDED_RECTANGLE, rectRadius: 0.2,
      fill: { color: tag === "SINAL" ? C.accent2 : C.accent4 }, color: tag === "SINAL" ? C.text2 : C.background1,
      fontSize: 11, bold: true, align: "center", valign: "middle", margin: 0, charSpacing: 1,
      objectName: "etiqueta passo " + (i + 1) });
    if (i < passos.length - 1) {
      s.addShape(pres.shapes.RIGHT_ARROW, { x: x + (2.1 + pd) / 2 + 0.12, y: 2.42, w: pdx - pd - 0.24, h: 0.32,
        fill: { color: C.background2 }, line: { color: C.background2 }, objectName: "seta " + (i + 1) });
    }
  });
  txt(s, [
    { text: "4 de 5 etapas são processamento de sinais. ", options: { bold: true, color: C.text2 } },
    { text: "A do meio (pensar) é toda feita de coisas contáveis: palavras, lances, posições.", options: { color: C.text1 } },
  ], { x: 0.75, y: 5.85, w: 11.8, h: 0.6, fontSize: 16, objectName: "rodapé viagem" });
  s.addNotes("Mostre o caminho completo. O celular só escuta e mostra; quem fala é o notebook, com a voz Piper, e o " +
    "robô mexe a boca no OLED no ritmo das palavras. Chame atenção para as etiquetas: SINAL (ondas, amostras) e " +
    "DISCRETA (coisas que dá para contar).");

  // ================= 3. som = numeros =================
  pres.addSection({ title: "Processamento de sinais" });
  s = pres.addSlide({ masterName: "CONTEUDO", sectionTitle: "Processamento de sinais" });
  s.addText("A voz do Zeus é só uma lista de números", { placeholder: "title" });
  const cats = onda.max.map((_, i) => (i % 10 === 0 ? (i / 100).toFixed(1).replace(".", ",") + " s" : ""));
  s.addChart(pres.charts.AREA, [
    { name: "pico", labels: cats, values: onda.max },
    { name: "vale", labels: cats, values: onda.min },
  ], {
    x: 0.6, y: 1.45, w: 7.4, h: 3.6, chartColors: [HEX.accent2, HEX.accent2],
    showTitle: true, title: "“Oi, tudo bem?” — a onda real (" + nf(DUR, 2) + " s)", titleFontSize: 14, titleColor: HEX.dk2,
    titleFontFace: "+mn-lt", showLegend: false, valAxisHidden: true, valGridLine: { style: "none" },
    catGridLine: { style: "none" }, catAxisLabelColor: HEX.accent6, catAxisLabelFontSize: 11,
    catAxisLabelFontFace: "+mn-lt", catAxisLineShow: false, catAxisLabelFrequency: 10, catAxisMajorTickMark: "none",
    objectName: "onda da voz",
  });
  s.addChart(pres.charts.LINE, [
    { name: "amostra", labels: onda.zoom.map((_, i) => String(i + 1)), values: onda.zoom },
  ], {
    x: 8.3, y: 1.45, w: 4.45, h: 3.6, chartColors: [HEX.accent3], lineSize: 1.5, lineDataSymbol: "circle",
    lineDataSymbolSize: 6, showTitle: true, title: "Zoom: 40 amostras (1,8 ms)", titleFontSize: 14,
    titleColor: HEX.dk2, titleFontFace: "+mn-lt", showLegend: false, valAxisHidden: true,
    valGridLine: { style: "none" }, catGridLine: { style: "none" }, catAxisHidden: true,
    objectName: "zoom das amostras",
  });
  const stats3 = [["22.050", "números por segundo", C.accent2], ["16 bits", "cada número (de −32.768 a 32.767)", C.accent3],
    [nf(onda.n), "números nessa frase", C.accent1]];
  stats3.forEach(([n, l, cor], i) => {
    const x = 0.6 + i * 4.1;
    s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x, y: 5.35, w: 3.85, h: 1.35, rectRadius: 0.12,
      fill: { color: C.background2 }, line: { color: C.background2 }, objectName: "cartão número " + (i + 1) });
    s.addShape(pres.shapes.OVAL, { x: x + 0.25, y: 5.75, w: 0.55, h: 0.55, fill: { color: cor }, line: { color: cor },
      objectName: "bolinha número " + (i + 1) });
    txt(s, n, { x: x + 1.0, y: 5.42, w: 2.75, h: 0.7, fontSize: 30, bold: true, color: C.text2, margin: 0,
      valign: "middle", objectName: "valor número " + (i + 1) });
    txt(s, l, { x: x + 1.0, y: 6.1, w: 2.75, h: 0.5, fontSize: 13, color: C.text1, margin: 0, valign: "top",
      objectName: "legenda número " + (i + 1) });
  });
  s.addNotes("Esta é a onda REAL do Zeus dizendo 'Oi, tudo bem?', gerada pela voz Piper no notebook. " +
    "À esquerda, o contorno da onda; à direita, um zoom com 40 pontinhos: cada um é UMA amostra. " +
    "Brincadeira com a plateia: peça para baterem palmas 22 mil vezes em um segundo. O computador faz isso fácil.");

  // ================= 4. fazendo a conta =================
  s = pres.addSlide({ masterName: "CONTEUDO", sectionTitle: "Processamento de sinais" });
  s.addText("Quanto pesa um “Oi, tudo bem?”", { placeholder: "title" });
  const contas = [
    ["Quantos números?", nf(DUR, 3) + " s × 22.050", "= " + nf(onda.n), "amostras", C.accent2],
    ["Quanto pesa?", nf(onda.n) + " × 2 bytes", "= " + nf(onda.n * 2), "bytes ≈ " + nf(onda.n * 2 / 1024, 1) + " KB", C.accent3],
    ["Até que agudo?", "22.050 ÷ 2", "= 11.025", "Hz (limite de Nyquist)", C.accent1],
  ];
  contas.forEach(([q, conta, res, un, cor], i) => {
    const x = 0.6 + i * 4.1;
    s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x, y: 1.55, w: 3.85, h: 3.65, rectRadius: 0.15,
      fill: { color: C.background1 }, line: { color: C.background2, width: 1 }, shadow: sombra(),
      objectName: "conta " + (i + 1) });
    txt(s, String(i + 1), { x: x + 0.3, y: 1.8, w: 0.6, h: 0.6, shape: pres.shapes.OVAL, fill: { color: cor },
      color: C.text2, fontSize: 20, bold: true, align: "center", valign: "middle", margin: 0,
      objectName: "número conta " + (i + 1) });
    txt(s, q, { x: x + 1.05, y: 1.8, w: 2.6, h: 0.6, fontSize: 20, bold: true, color: C.text2, margin: 0,
      valign: "middle", objectName: "pergunta conta " + (i + 1) });
    txt(s, conta, { x: x + 0.3, y: 2.7, w: 3.25, h: 0.5, fontSize: 18, color: C.text1, margin: 0,
      fontFace: "Courier New", objectName: "expressão conta " + (i + 1) });
    txt(s, res, { x: x + 0.3, y: 3.25, w: 3.25, h: 0.9, fontSize: 40, bold: true, color: C.text2, margin: 0,
      valign: "middle", objectName: "resultado conta " + (i + 1) });
    txt(s, un, { x: x + 0.3, y: 4.15, w: 3.25, h: 0.5, fontSize: 15, color: C.text1, margin: 0,
      objectName: "unidade conta " + (i + 1) });
  });
  txt(s, [
    { text: "Por que 22.050 basta? ", options: { bold: true, color: C.text2 } },
    { text: "Nyquist: para guardar um som de frequência f, é preciso amostrar a pelo menos 2f. " +
      "A voz que a gente entende vai até uns 8.000 Hz, e 11.025 Hz passa com folga.", options: { color: C.text1 } },
  ], { x: 0.6, y: 5.45, w: 12.1, h: 0.7, fontSize: 16, objectName: "explicação Nyquist" });
  txt(s, [
    { text: "Curiosidade: ", options: { bold: true, color: C.accent3 } },
    { text: "1 minuto de Zeus falando = 60 × 22.050 = 1.323.000 números.", options: { color: C.text1 } },
  ], { x: 0.6, y: 6.15, w: 11.0, h: 0.5, fontSize: 16, objectName: "curiosidade" });
  s.addNotes("Faça as contas junto com a plateia. Os valores são os da frase real do slide anterior. " +
    "O arquivo WAV que o notebook toca tem " + nf(onda.bytes) + " bytes: " + nf(onda.n * 2) + " de som + 44 de cabeçalho. " +
    "Nyquist: se amostrar devagar demais, os agudos viram 'fantasmas' graves (aliasing), como a roda da carroça girando ao contrário no filme.");

  // ================= 5. Zeus ou Deus =================
  s = pres.addSlide({ masterName: "CONTEUDO", sectionTitle: "Processamento de sinais" });
  s.addText("“Zeus” ou “Deus”? O celular chuta 5 vezes", { placeholder: "title" });
  s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: 0.6, y: 1.7, w: 4.6, h: 1.6, rectRadius: 0.25,
    fill: { color: C.accent1 }, line: { color: C.accent1 }, objectName: "balão de fala" });
  s.addShape(pres.shapes.ISOSCELES_TRIANGLE, { x: 1.2, y: 3.25, w: 0.6, h: 0.5, rotate: 180,
    fill: { color: C.accent1 }, line: { color: C.accent1 }, objectName: "ponta do balão" });
  txt(s, "“Zeus, que horas são?”", { x: 0.8, y: 1.8, w: 4.2, h: 1.4, fontSize: 26, bold: true, color: C.text2,
    align: "center", valign: "middle", objectName: "frase falada" });
  bolinhaIcone(s, I.ouvido, 0.6, 4.0, 1.0, C.accent2, "ouvido");
  txt(s, "O som chega borrado: “Z” e “D” são parecidos. Então o celular devolve vários palpites, do mais provável ao menos.",
    { x: 1.8, y: 3.95, w: 3.4, h: 1.5, fontSize: 15, color: C.text1, margin: 0, valign: "top", objectName: "explicação palpites" });
  const palpites = [
    ["viu que horas são", "sem o nome → pula", false],
    ["Zeus que horas são", "tem o nome → ESCOLHIDO", true],
    ["Deus que horas são", "também valeria (no começo da frase)", false],
    ["seus que horas são", "não chega a ser lido", false],
    ["eu que horas são", "não chega a ser lido", false],
  ];
  palpites.forEach(([p, nota, ok], i) => {
    const y = 1.6 + i * 0.95;
    s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: 5.8, y, w: 6.95, h: 0.78, rectRadius: 0.12,
      fill: { color: ok ? C.accent5 : C.background2 }, line: { color: ok ? C.accent5 : C.background2 },
      objectName: "palpite " + (i + 1) });
    txt(s, "#" + (i + 1), { x: 5.95, y, w: 0.6, h: 0.78, fontSize: 16, bold: true, color: ok ? C.text2 : C.accent6,
      valign: "middle", margin: 0, objectName: "posição palpite " + (i + 1) });
    txt(s, p, { x: 6.6, y, w: 3.1, h: 0.78, fontSize: 18, bold: ok, color: C.text2, valign: "middle", margin: 0,
      objectName: "texto palpite " + (i + 1) });
    txt(s, nota, { x: 9.7, y, w: 2.95, h: 0.78, fontSize: 13, color: C.text1, valign: "middle", margin: 0,
      italic: !ok, objectName: "nota palpite " + (i + 1) });
  });
  txt(s, [
    { text: "Filtro de barulho: ", options: { bold: true, color: C.text2 } },
    { text: "“hum”, “ah”, “ok” sozinhos são descartados antes de chegar ao cérebro.", options: { color: C.text1 } },
  ], { x: 0.6, y: 6.5, w: 12.1, h: 0.5, fontSize: 15, objectName: "filtro de barulho" });
  s.addNotes("Caso real: no Galaxy S21, 'Zeus que horas' virou 'viu que horas' no primeiro palpite. " +
    "Solução: pedir 5 alternativas (maxAlternatives = 5) e escolher a primeira que tem o nome do robô. " +
    "Os palpites 3 a 5 aqui são ilustrativos. Brinque: peça para alguém falar 'Zeus' e 'Deus' bem rápido.");

  // ================= 5b. a voz e o Ollama =================
  // medido em 2026-10-27 no llama3.1:8b deste notebook: tokens por palavra = prompt_eval_count
  // de prefixos crescentes (modo raw); resposta e velocidade = /api/chat com temperature 0.7
  const TOKENS = [["Zeus", 2], [",", 1], ["boa", 1], ["noite", 2], ["!", 1], ["Tudo", 2], ["bem", 1], ["?", 1]];
  const RESPOSTA = { texto: "Sim, tudo bem, estou funcionando normalmente, obrigado!", tokens: 16, s: 0.276 };
  s = pres.addSlide({ masterName: "CONTEUDO", sectionTitle: "Processamento de sinais" });
  s.addText("Do ar até o Ollama (e de volta)", { placeholder: "title" });
  const etapas = [
    [I.mic, C.accent1, "O ouvido", "O ar treme e o microfone do celular vira isso em números.", "som → números", true],
    [I.texto, C.accent2, "O tradutor", "O reconhecedor de voz transforma os números em letras.", "números → texto", true],
    [I.picadinho, C.accent4, "O picadinho", "O texto é cortado em pedacinhos: os tokens.", "texto → tokens", false],
    [I.cerebro, C.accent4, "O cérebro", "Adivinha o próximo pedacinho, um por vez, igual ao corretor do teclado.", "tokens → texto", false],
    [I.som, C.accent3, "A boca", "O Piper desenha a onda de novo: 22.050 números por segundo.", "texto → som", true],
  ];
  etapas.forEach(([img, cor, tit, desc, chip, audio], i) => {
    const x = 0.6 + i * 2.45;
    s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x, y: 1.45, w: 2.3, h: 2.45, rectRadius: 0.15,
      fill: { color: C.background2 }, line: { color: C.background2 }, objectName: "etapa " + (i + 1) });
    bolinhaIcone(s, img, x + 0.75, 1.6, 0.8, cor, "etapa " + (i + 1));
    txt(s, tit, { x: x + 0.1, y: 2.45, w: 2.1, h: 0.35, fontSize: 16, bold: true, color: C.text2, align: "center", margin: 0,
      objectName: "título etapa " + (i + 1) });
    txt(s, desc, { x: x + 0.15, y: 2.8, w: 2.0, h: 0.62, fontSize: 12, color: C.text1, align: "center", valign: "top",
      margin: 0, objectName: "descrição etapa " + (i + 1) });
    txt(s, (audio ? "ÁUDIO · " : "OLLAMA · ") + chip, { x: x + 0.15, y: 3.48, w: 2.0, h: 0.3,
      shape: pres.shapes.ROUNDED_RECTANGLE, rectRadius: 0.15, fill: { color: audio ? C.accent2 : C.accent4 },
      color: audio ? C.text2 : C.background1, fontSize: 10, bold: true, align: "center", valign: "middle", margin: 0,
      objectName: "etiqueta etapa " + (i + 1) });
  });
  // a frase como o Ollama enxerga
  txt(s, "Como o Ollama enxerga a frase:", { x: 0.6, y: 4.1, w: 7.0, h: 0.35, fontSize: 15, bold: true, color: C.text2,
    margin: 0, objectName: "título tokens" });
  let cx = 0.6;
  TOKENS.forEach(([p, n], i) => {
    const w = 0.45 + p.length * 0.13;
    txt(s, [{ text: p, options: { fontSize: 15 } }, { text: " ×" + n, options: { fontSize: 10, color: C.text2, bold: false } }],
      { x: cx, y: 4.55, w, h: 0.5, shape: pres.shapes.ROUNDED_RECTANGLE, rectRadius: 0.1,
        fill: { color: [C.accent1, C.accent2, C.accent5, C.accent3][i % 4] }, color: C.text2, bold: true,
        align: "center", valign: "middle", margin: 0, objectName: "pedaço " + p });
    cx += w + 0.06;
  });
  txt(s, [
    { text: "= 11 pedacinhos (tokens). ", options: { bold: true, color: C.text2 } },
    { text: "Cada um vira um número do dicionário de 128.256 pedacinhos do Llama 3.1.", options: { color: C.text1 } },
  ], { x: 0.6, y: 5.15, w: 7.0, h: 0.4, fontSize: 14, margin: 0, objectName: "total tokens" });
  s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: 0.6, y: 5.7, w: 7.0, h: 1.05, rectRadius: 0.15,
    fill: { color: C.background1 }, line: { color: C.accent4, width: 1.5 }, objectName: "balão resposta" });
  txt(s, [
    { text: "Resposta de verdade do Zeus: ", options: { bold: true, color: C.accent4, breakLine: false } },
    { text: "“" + RESPOSTA.texto + "”", options: { color: C.text2, italic: true, breakLine: true } },
    { text: RESPOSTA.tokens + " pedacinhos escritos em " + nf(RESPOSTA.s, 2) + " s: uns " +
      nf(RESPOSTA.tokens / RESPOSTA.s, 0) + " por segundo.", options: { color: C.text1 } },
  ], { x: 0.8, y: 5.75, w: 6.6, h: 0.95, fontSize: 13, margin: 0, valign: "middle", objectName: "texto resposta" });
  // analogia do restaurante
  s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: 7.95, y: 4.1, w: 4.8, h: 2.65, rectRadius: 0.15,
    fill: { color: C.text2 }, line: { color: C.text2 }, objectName: "cartão restaurante" });
  bolinhaIcone(s, I.talheres, 8.2, 4.3, 0.7, C.accent1, "restaurante");
  txt(s, "O garçom e o cozinheiro", { x: 9.05, y: 4.3, w: 3.55, h: 0.7, fontSize: 18, bold: true, color: C.background1,
    margin: 0, valign: "middle", objectName: "título restaurante" });
  txt(s, [
    { text: "O garçom (áudio) ouve o cliente e anota o pedido. ", options: { color: C.background1 } },
    { text: "O cozinheiro (Ollama) só lê a comanda: nunca ouve ninguém. ", options: { color: C.accent1, bold: true } },
    { text: "Depois o garçom leva o prato e fala com o cliente (a voz do Zeus).", options: { color: C.background1 } },
  ], { x: 8.2, y: 5.1, w: 4.35, h: 1.5, fontSize: 14, margin: 0, valign: "top", objectName: "texto restaurante" });
  s.addNotes("Mensagem principal: o LLM (Llama 3.1 8B no Ollama) não ouve som nem fala. Ele só lê e escreve texto. " +
    "Quem cuida do som é o processamento de áudio nas duas pontas: o reconhecedor de voz do celular (som → texto) e a voz " +
    "Piper no notebook (texto → som). Fluxo real do Zeus: celular reconhece a fala, a página manda o texto para a ponte " +
    "(/llm), a ponte repassa ao Ollama local, a resposta volta em texto e o Piper fala no alto-falante enquanto o OLED mexe a boca. " +
    "Tokens: o modelo não lê letras nem palavras inteiras, e sim pedaços (tokens). 'Zeus', 'noite' e 'Tudo' viram 2 pedaços " +
    "cada; os números foram medidos neste notebook com o llama3.1:8b. O modelo escolhe um pedaço por vez, como o corretor do " +
    "teclado do celular, só que com 8 bilhões de parâmetros. Brincadeira: peça para a plateia completar 'Zeus, boa...' — " +
    "todo mundo diz 'noite'. É exatamente o que o modelo faz.");

  // ================= 6. boca =================
  s = pres.addSlide({ masterName: "CONTEUDO", sectionTitle: "Processamento de sinais" });
  s.addText("A boca do Zeus segue o ritmo da fala", { placeholder: "title" });
  bolinhaIcone(s, I.karaoke, 0.6, 1.35, 0.6, C.accent1, "karaokê");
  txt(s, [
    { text: "Igual à bolinha do karaokê: ", options: { bold: true, color: C.text2 } },
    { text: "ela pula para cada palavra na hora certa. O Zeus faz a mesma conta para abrir a boca no OLED.",
      options: { color: C.text1 } },
  ], { x: 1.4, y: 1.35, w: 11.3, h: 0.6, fontSize: 16, margin: 0, valign: "middle", objectName: "analogia karaokê" });
  const t0 = 0.9, tw = 11.5, dur = DUR, yb = 3.05;
  const tx = (t) => t0 + (t / dur) * tw;
  s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: t0, y: yb, w: tw, h: 0.22, rectRadius: 0.11,
    fill: { color: C.background2 }, line: { color: C.background2 }, objectName: "linha do tempo" });
  [["0 s", 0], ["0,5 s", 0.5], [nf(dur, 2) + " s", dur]].forEach(([l, t], i) => {
    txt(s, l, { x: tx(t) - 0.5, y: yb + 0.3, w: 1.0, h: 0.3, fontSize: 12, color: C.accent6, align: "center", margin: 0,
      objectName: "tempo " + (i + 1) });
  });
  const marcas = [["Oi,", MARCA(0), C.accent1], ["tudo", MARCA(5), C.accent2], ["bem?", MARCA(10), C.accent3]];
  marcas.forEach(([p, t, cor]) => {
    const x = tx(t);
    s.addShape(pres.shapes.OVAL, { x: x - 0.2, y: yb - 0.09, w: 0.4, h: 0.4, fill: { color: cor }, line: { color: C.background1, width: 2 },
      objectName: "marca " + p });
    txt(s, p, { x: x - 0.8, y: yb - 0.95, w: 1.6, h: 0.45, fontSize: 22, bold: true, color: C.text2, align: "center", margin: 0,
      objectName: "palavra " + p });
    txt(s, nf(t, 2) + " s", { x: x - 0.8, y: yb - 0.5, w: 1.6, h: 0.3, fontSize: 13, color: C.text1,
      align: "center", margin: 0, objectName: "instante " + p });
  });
  const cab = (t, al) => ({ text: t, options: { bold: true, color: C.background1, fill: { color: C.text2 }, align: al } });
  const cen = (t, b) => ({ text: t, options: { align: "center", bold: !!b } });
  s.addTable([
    [cab("Palavra", "left"), cab("Letras", "center"), cab("Pausa", "center"), cab("Peso", "center")],
    ["Oi,", cen("2"), cen("+3 (vírgula)"), cen("5", 1)],
    ["tudo", cen("4"), cen("+1"), cen("5", 1)],
    ["bem?", cen("3"), cen("+6 (ponto)"), cen("9", 1)],
    [{ text: "Total", options: { bold: true } }, "", "", cen("19", 1)],
  ], { x: 0.6, y: 4.0, w: 5.6, colW: [1.3, 1.1, 2.0, 1.2], rowH: 0.46, fontSize: 15, color: C.text1,
    border: { type: "solid", pt: 0.75, color: HEX.lt2 }, fill: { color: C.background1 }, objectName: "tabela de pesos" });
  s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: 6.55, y: 4.0, w: 6.2, h: 2.75, rectRadius: 0.15,
    fill: { color: C.text2 }, line: { color: C.text2 }, objectName: "cartão regra de três" });
  txt(s, "A conta é uma regra de três, em 3 passos:", { x: 6.85, y: 4.12, w: 5.7, h: 0.4, fontSize: 16, bold: true,
    color: C.accent1, margin: 0, objectName: "título regra de três" });
  txt(s, [
    { text: "1. Mede a duração: ", options: { bold: true, color: C.background1, breakLine: false } },
    { text: nf(onda.n) + " amostras ÷ 22.050 = " + nf(DUR, 3) + " s", options: { color: C.accent6, breakLine: true } },
    { text: "2. Dá um peso a cada palavra ", options: { bold: true, color: C.background1, breakLine: false } },
    { text: "(como dividir a conta do bar pelo que cada um consumiu)", options: { color: C.accent6, breakLine: true } },
    { text: "3. Cada palavra começa na fração do tempo já “gasta”:", options: { bold: true, color: C.background1 } },
  ], { x: 6.85, y: 4.55, w: 5.7, h: 1.45, fontSize: 14, margin: 0, valign: "top", paraSpaceAfter: 4, objectName: "passos regra de três" });
  txt(s, "0,05 + duração × 0,95 × (anteriores ÷ 19)", { x: 6.85, y: 5.8, w: 5.7, h: 0.35, fontSize: 13,
    color: C.background1, margin: 0, fontFace: "Courier New", objectName: "fórmula regra de três" });
  txt(s, "“bem?”: 0,05 + " + nf(DUR, 3) + " × 0,95 × 10 ÷ 19 = " + nf(MARCA(10), 2) + " s", { x: 6.85, y: 6.2, w: 5.7, h: 0.4,
    fontSize: 15, bold: true, color: C.accent2, margin: 0, objectName: "exemplo regra de três" });
  s.addNotes("O notebook sabe a duração exata do áudio (amostras ÷ 22.050). Ele divide esse tempo entre as palavras: " +
    "palavra comprida ganha mais tempo, e pontuação ganha pausa. Fórmula completa: início = 0,05 + duração × 0,95 × " +
    "(soma dos pesos anteriores ÷ soma total). O 0,05 s é um pequeno atraso inicial e o 0,95 deixa uma folga no fim. " +
    "A cada marca, a ponte manda um pulso de boca para o OLED. Não há análise do som: é só proporção, como o karaokê.");

  // ================= passo a passo (engenharia) =================
  const etiquetaEng = (s) => txt(s, "ENGENHARIA", { x: 10.9, y: 0.6, w: 1.8, h: 0.42, shape: pres.shapes.ROUNDED_RECTANGLE,
    rectRadius: 0.2, fill: { color: C.accent4 }, color: C.background1, fontSize: 12, bold: true, align: "center",
    valign: "middle", margin: 0, charSpacing: 1, objectName: "etiqueta engenharia" });
  // linha de passo: esquerda = numero, titulo, formula e "em palavras"; direita = quadro "no dia a dia"
  const passoAPasso = (s, lista, y, alt, cor) => lista.forEach(([tit, formula, palavras, diaADia], i) => {
    const yy = y + i * alt, n = i + 1;
    txt(s, String(n), { x: 0.6, y: yy + 0.02, w: 0.42, h: 0.42, shape: pres.shapes.OVAL, fill: { color: cor },
      color: C.text2, fontSize: 14, bold: true, align: "center", valign: "middle", margin: 0, objectName: "passo " + n });
    txt(s, tit, { x: 1.15, y: yy, w: 5.45, h: 0.32, fontSize: 15, bold: true, color: C.text2, margin: 0,
      valign: "middle", objectName: "título passo " + n });
    txt(s, formula, { x: 1.15, y: yy + 0.32, w: 5.45, h: 0.3, fontSize: 13, color: C.accent4, margin: 0,
      fontFace: "Courier New", bold: true, valign: "middle", objectName: "fórmula passo " + n });
    txt(s, palavras, { x: 1.15, y: yy + 0.63, w: 5.45, h: alt - 0.7, fontSize: 12, color: C.text1, margin: 0,
      valign: "top", objectName: "em palavras passo " + n });
    s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: 6.85, y: yy, w: 5.9, h: alt - 0.12, rectRadius: 0.1,
      fill: { color: C.background2 }, line: { color: C.background2 }, objectName: "quadro dia a dia " + n });
    txt(s, [
      { text: "NO DIA A DIA  ", options: { bold: true, color: C.accent4, fontSize: 11, charSpacing: 1 } },
      { text: diaADia, options: { color: C.text2 } },
    ], { x: 7.05, y: yy + 0.06, w: 5.55, h: alt - 0.24, fontSize: 13, margin: 0, valign: "middle",
      objectName: "texto dia a dia " + n });
  });
  // slide de intuicao: grafico a esquerda, analogia a direita
  const analogia = (s, img, cor, titulo, texto, pergunta) => {
    s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: 8.0, y: 1.5, w: 4.75, h: 4.6, rectRadius: 0.15,
      fill: { color: C.background2 }, line: { color: C.background2 }, objectName: "cartão analogia" });
    bolinhaIcone(s, img, 8.3, 1.75, 0.9, cor, "analogia");
    txt(s, titulo, { x: 9.4, y: 1.75, w: 3.15, h: 0.9, fontSize: 20, bold: true, color: C.text2, margin: 0,
      valign: "middle", objectName: "título analogia" });
    txt(s, texto, { x: 8.3, y: 2.85, w: 4.2, h: 3.1, fontSize: 15, color: C.text1, margin: 0, valign: "top",
      paraSpaceAfter: 6, objectName: "texto analogia" });
    txt(s, pergunta, { x: 0.6, y: 6.3, w: 12.1, h: 0.5, fontSize: 16, bold: true, color: C.accent3, margin: 0,
      objectName: "pergunta para a plateia" });
  };
  const fft = onda.fft, ac = onda.auto, rc = onda.recorte;
  const graf = { titleFontSize: 14, titleColor: HEX.dk2, titleFontFace: "+mn-lt", showLegend: false,
    valAxisLabelColor: HEX.accent6, valAxisLabelFontSize: 11, valAxisLabelFontFace: "+mn-lt",
    catAxisLabelColor: HEX.accent6, catAxisLabelFontSize: 11, catAxisLabelFontFace: "+mn-lt",
    valGridLine: { color: HEX.lt2, size: 0.75 }, catGridLine: { style: "none" }, lineSize: 2, lineDataSymbol: "none" };

  // ----- A. quantizacao: intuicao -----
  s = pres.addSlide({ masterName: "CONTEUDO", sectionTitle: "Processamento de sinais" });
  s.addText("Por que o som vira degraus?", { placeholder: "title" });
  etiquetaEng(s);
  const NQ = 48, QB = 3, QD = 2 / 2 ** QB;
  const seno = Array.from({ length: NQ }, (_, i) => Math.sin((2 * Math.PI * i) / (NQ - 1)) * 0.98);
  const quant = seno.map((v) => Math.min(1 - QD / 2, Math.max(-1 + QD / 2, QD * (Math.floor(v / QD) + 0.5))));
  s.addChart(pres.charts.LINE, [
    { name: "som real", labels: seno.map((_, i) => String(i)), values: seno.map((v) => +v.toFixed(3)) },
    { name: "guardado com 3 bits", labels: seno.map((_, i) => String(i)), values: quant.map((v) => +v.toFixed(3)) },
  ], Object.assign({}, graf, {
    x: 0.6, y: 1.45, w: 7.1, h: 4.7, chartColors: [HEX.accent2, HEX.accent3], showTitle: true,
    title: "Uma onda guardada com só 3 bits (8 degraus)", showLegend: true, legendPos: "b", legendFontSize: 13,
    legendFontFace: "+mn-lt", legendColor: HEX.dk1, valAxisHidden: true, catAxisHidden: true,
    valGridLine: { style: "none" }, objectName: "quantização ilustrada",
  }));
  analogia(s, I.regua, C.accent2, "A régua com poucas marquinhas", [
    { text: "Meça sua altura com uma régua que só marca de 10 em 10 cm: 1,73 m vira 1,70 m.", options: { breakLine: true } },
    { text: "O computador faz o mesmo com o som: cada amostra cai na marquinha mais próxima.", options: { breakLine: true } },
    { text: "Com 3 bits são só 8 marquinhas (linha vermelha). O Zeus usa 16 bits: 65.536 marquinhas.", options: { bold: true } },
  ], "Pergunta: quantas marquinhas o ouvido precisa para não perceber o degrau?");
  s.addNotes("Slide de intuição antes da conta. Quantizar = arredondar cada amostra para um nível permitido. " +
    "Com poucos bits, a voz sai 'metálica' ou com chiado. Deixe a pergunta no ar: a resposta é o SNR do próximo slide.");

  // ----- A. quantizacao: passo a passo -----
  s = pres.addSlide({ masterName: "CONTEUDO", sectionTitle: "Processamento de sinais" });
  s.addText("Passo a passo 1: quantizar e transmitir", { placeholder: "title" });
  etiquetaEng(s);
  passoAPasso(s, [
    ["Taxa de bits do áudio do Zeus", "R = 22.050 × 16 × 1 = 352.800 bit/s",
      "22.050 amostras/s × 16 bits × 1 canal (mono). OBS: 1 byte = 8 bits, então 352.800 ÷ 8 = 44.100 bytes/s; " +
      "a frase de " + nf(DUR, 3) + " s tem " + nf(onda.n * 2) + " bytes.",
      "É o “consumo de dados” da voz: 1 minuto de Zeus falando = 2,6 MB, quase uma foto de celular. " +
      "Por isso o celular manda só o texto, e o notebook gera o som."],
    ["Níveis e degrau de quantização", "L = 2¹⁶ = 65.536    Δ = 2·Xmax / L",
      "16 bits dão 65.536 valores, de −32.768 a 32.767. Δ é a distância entre dois valores vizinhos (1 degrau).",
      "Uma escada com 65.536 degraus: você só pode pisar nos degraus, nunca no meio. Quanto mais degraus, mais suave a subida."],
    ["Erro de arredondamento", "e ~ U(−Δ/2, Δ/2)  ⇒  σ² = Δ² / 12",
      "O erro fica entre −½ e +½ degrau, todos com a mesma chance (distribuição uniforme). A variância dela é Δ²/12.",
      "Arredondar o preço para o real mais próximo: R$ 4,70 vira R$ 5 (+0,30) e R$ 4,20 vira R$ 4 (−0,20). " +
      "Nunca passa de 50 centavos e, na média, empata."],
    ["SNR teórica (senoide em escala cheia)", "SNR = 6,02·b + 1,76 = 98,1 dB",
      "SNR = 10·log₁₀(Ps/σ²), com Ps = Xmax²/2. Cada bit a mais divide o degrau por 2 e soma ~6 dB.",
      "Se a voz do Zeus tivesse o volume de uma britadeira (~100 dB), o chiado do arredondamento teria ~2 dB: " +
      "abaixo do que o ouvido humano escuta."],
  ], 1.5, 1.32, C.accent2);
  s.addNotes("Derivação do passo 4: senoide de amplitude Xmax tem potência Ps = Xmax²/2. Com Δ = 2·Xmax/2^b, " +
    "σ² = Δ²/12 = Xmax²/(3·2^(2b)). Então Ps/σ² = 1,5·2^(2b) e SNR = 10·log10(1,5) + 20·b·log10(2) = 1,76 + 6,02·b dB. " +
    "Com b = 16 dá 98,1 dB; com 3 bits (slide anterior) seria 19,8 dB, e o chiado seria audível. " +
    "1 minuto = 60 × 44.100 = 2.646.000 bytes ≈ 2,6 MB.");

  // tirinha de 4 quadros: icone, acao simples, exemplo do dia a dia e a formula pequena no rodape
  const quadrinhos = (s, lista, cor) => lista.forEach(([img, tit, oQueFaz, diaADia, formula], i) => {
    const x = 0.6 + i * 3.08, n = i + 1;
    s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x, y: 1.45, w: 2.9, h: 4.5, rectRadius: 0.15,
      fill: { color: C.background2 }, line: { color: C.background2 }, objectName: "quadro " + n });
    bolinhaIcone(s, img, x + 0.95, 1.6, 1.0, cor, "quadro " + n);
    txt(s, String(n), { x: x + 0.2, y: 1.6, w: 0.45, h: 0.45, shape: pres.shapes.OVAL, fill: { color: C.text2 },
      color: C.background1, fontSize: 15, bold: true, align: "center", valign: "middle", margin: 0, objectName: "número quadro " + n });
    txt(s, tit, { x: x + 0.15, y: 2.7, w: 2.6, h: 0.45, fontSize: 18, bold: true, color: C.text2, align: "center",
      valign: "middle", margin: 0, objectName: "título quadro " + n });
    txt(s, oQueFaz, { x: x + 0.2, y: 3.15, w: 2.5, h: 0.75, fontSize: 14, color: C.text1, align: "center", valign: "top",
      margin: 0, objectName: "ação quadro " + n });
    s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: x + 0.15, y: 3.95, w: 2.6, h: 1.35, rectRadius: 0.1,
      fill: { color: C.background1 }, line: { color: C.background1 }, objectName: "dia a dia quadro " + n });
    txt(s, [
      { text: "NO DIA A DIA", options: { bold: true, color: C.accent4, fontSize: 10, charSpacing: 1, breakLine: true } },
      { text: diaADia, options: { color: C.text2, fontSize: 13 } },
    ], { x: x + 0.28, y: 4.0, w: 2.34, h: 1.25, margin: 0, valign: "middle", objectName: "texto dia a dia quadro " + n });
    txt(s, formula, { x: x + 0.1, y: 5.42, w: 2.7, h: 0.4, fontSize: 11, color: C.accent4, bold: true, align: "center",
      valign: "middle", margin: 0, fontFace: "Courier New", objectName: "fórmula quadro " + n });
    if (i < lista.length - 1) s.addShape(pres.shapes.RIGHT_ARROW, { x: x + 2.92, y: 2.0, w: 0.14, h: 0.3,
      fill: { color: C.accent6 }, line: { color: C.accent6 }, objectName: "seta quadro " + n });
  });
  const faixa = (s, partes) => {
    s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: 0.6, y: 6.1, w: 12.15, h: 0.7, rectRadius: 0.12,
      fill: { color: C.text2 }, line: { color: C.text2 }, objectName: "faixa conclusão" });
    txt(s, partes, { x: 0.85, y: 6.1, w: 11.7, h: 0.7, fontSize: 16, margin: 0, valign: "middle", objectName: "texto conclusão" });
  };
  const razao = fft.f_pico / ac.f0;

  // ----- B. FFT: intuicao -----
  s = pres.addSlide({ masterName: "CONTEUDO", sectionTitle: "Processamento de sinais" });
  s.addText("A receita secreta da voz", { placeholder: "title" });
  etiquetaEng(s);
  // dispersao: eixo x numerico (em linha, cada bin vira categoria e os rotulos quebram)
  s.addChart(pres.charts.SCATTER, [{ name: "f (Hz)", values: fft.freq },
    { name: "|X[k]| (dB)", values: fft.db.map((v) => Math.max(v, -60)) }], Object.assign({}, graf, {
    x: 0.6, y: 1.45, w: 7.1, h: 4.7, chartColors: [HEX.accent5], showTitle: true,
    title: "Os “ingredientes” da vogal de “bem” (Hz)", catAxisMinVal: 0, catAxisMaxVal: 2000, catAxisMajorUnit: 500,
    catLabelFormatCode: "0", valAxisCrossesAt: "min", valAxisMinVal: -60, valAxisMaxVal: 0, valAxisMajorUnit: 20,
    objectName: "espectro FFT",
  }));
  analogia(s, I.liquidificador, C.accent5, "A vitamina de frutas", [
    { text: "Você toma uma vitamina e quer saber a receita: quanto tem de banana, de morango, de maçã?", options: { breakLine: true } },
    { text: "A voz também é uma mistura. A FFT descobre a receita: quanto tem de cada som, do grave ao agudo.", options: { breakLine: true } },
    { text: "Cada pico do gráfico é um ingrediente. O mais forte está em " + nf(fft.f_pico, 0) + " Hz.", options: { bold: true } },
  ], "Pergunta: o ingrediente mais forte (" + nf(fft.f_pico, 0) + " Hz) é o tom de voz do Zeus?");
  s.addNotes("Slide de intuição. A FFT (transformada rápida de Fourier) decompõe o sinal em senoides. Mostre que a voz " +
    "não é uma frequência só: é uma fundamental e vários múltiplos dela (harmônicos), espaçados de ~" +
    nf(fft.harmonicos[1] - fft.harmonicos[0], 0) + " Hz. Harmônicos detectados (Hz): " +
    fft.harmonicos.map((v) => nf(v, 1)).join(", ") + ". Deixe a pergunta no ar; a resposta vem dois slides adiante.");

  // ----- B. FFT: passo a passo -----
  s = pres.addSlide({ masterName: "CONTEUDO", sectionTitle: "Processamento de sinais" });
  s.addText("Passo a passo 2: a receita (FFT)", { placeholder: "title" });
  etiquetaEng(s);
  quadrinhos(s, [
    [I.colher, "Provar uma colher", "Pega só " + nf(rc.ms, 0) + " ms da vogal “e” de “bem”.",
      "Para saber o gosto, você prova uma colher, não o copo todo.", "N = 1024 amostras"],
    [I.volume, "Suavizar as pontas", "Abaixa o som devagar no começo e no fim do pedaço.",
      "O DJ abaixa a música aos poucos: cortar de uma vez faz um “clique”.", "janela de Hann"],
    [I.barras, "Separar os sabores", "Mede quanto tem de cada som, em gavetas de " + nf(fft.df, 1) + " Hz.",
      "As barrinhas do equalizador do carro subindo e descendo.", "Δf = 22.050 ÷ 1024"],
    [I.lupa, "Achar o mais forte", "A gaveta mais cheia mostra o ingrediente principal.",
      "Na vitamina, o morango é o que mais aparece no gosto.", "f ≈ " + nf(fft.f_pico, 0) + " Hz"],
  ], C.accent5);
  faixa(s, [
    { text: "Resultado: ", options: { bold: true, color: C.accent1 } },
    { text: "o som mais forte está em " + nf(fft.f_pico, 0) + " Hz. Parece a voz do Zeus... mas será mesmo?",
      options: { color: C.background1 } },
  ]);
  s.addNotes("Versão técnica para quem quiser: recorte de N = 1024 amostras (" + nf(rc.ms, 1) + " ms, de " +
    nf(rc.t_ini, 3) + " a " + nf(rc.t_fim, 3) + " s); janela de Hann w[n] = 0,5·(1 − cos(2πn/(N−1))) para reduzir o " +
    "vazamento espectral; FFT X[k] = Σ x[n]·w[n]·e^(−j2πkn/N), com resolução Δf = fs/N = " + nf(fft.df, 2) +
    " Hz e custo N·log₂N = 10.240 contra N² = 1.048.576 da DFT direta; pico no bin " + fft.k_pico +
    " refinado por interpolação parabólica δ = ½(α−γ)/(α−2β+γ) = " + nf(fft.d_pico, 3) + ", então f = (" + fft.k_pico +
    " + δ) × Δf = " + nf(fft.f_pico, 1) + " Hz. Números reais calculados por analisar.py.");

  // ----- C. autocorrelacao: intuicao -----
  s = pres.addSlide({ masterName: "CONTEUDO", sectionTitle: "Processamento de sinais" });
  s.addText("As pegadas da voz", { placeholder: "title" });
  etiquetaEng(s);
  s.addChart(pres.charts.SCATTER, [{ name: "τ", values: ac.r.map((_, i) => i) }, { name: "r[τ]", values: ac.r }],
    Object.assign({}, graf, {
      x: 0.6, y: 1.45, w: 7.1, h: 3.6, chartColors: [HEX.accent3], showTitle: true,
      title: "Quanto a cópia atrasada se parece com a original", catAxisMinVal: 0, catAxisMaxVal: 350, catAxisMajorUnit: 50,
      catLabelFormatCode: "0", valAxisCrossesAt: "min", valAxisMinVal: -1, valAxisMaxVal: 1, valAxisMajorUnit: 1,
      objectName: "autocorrelação",
    }));
  // tres momentos reais do grafico
  const vale = ac.r.reduce((m, v, i) => (i >= 20 && i < 120 && v < ac.r[m] ? i : m), 20);
  [
    ["Atraso 0", "r = 1", "a cópia está em cima da original: igualzinho", C.accent5],
    ["Atraso " + vale, "r = " + nf(ac.r[vale], 1), "pegada em cima do vão: fica “ao contrário”", C.accent6],
    ["Atraso " + ac.tau, "r = " + nf(ac.r_tau, 2), "a próxima pegada encaixa: quase igual de novo!", C.accent3],
  ].forEach(([tit, val, desc, cor], i) => {
    const x = 0.6 + i * 2.4;
    s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x, y: 5.2, w: 2.3, h: 1.0, rectRadius: 0.1,
      fill: { color: C.background2 }, line: { color: cor, width: 2 }, objectName: "momento " + (i + 1) });
    txt(s, [{ text: tit + "  ", options: { bold: true, color: C.text2 } }, { text: val, options: { bold: true, color: cor === C.accent6 ? C.text1 : cor } }],
      { x: x + 0.12, y: 5.25, w: 2.1, h: 0.35, fontSize: 14, margin: 0, valign: "middle", objectName: "título momento " + (i + 1) });
    txt(s, desc, { x: x + 0.12, y: 5.6, w: 2.1, h: 0.55, fontSize: 12, color: C.text1, margin: 0, valign: "top",
      objectName: "texto momento " + (i + 1) });
  });
  s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: 8.0, y: 1.45, w: 4.75, h: 4.75, rectRadius: 0.15,
    fill: { color: C.background2 }, line: { color: C.background2 }, objectName: "cartão analogia" });
  bolinhaIcone(s, I.pegadas, 8.25, 1.65, 0.8, C.accent3, "pegadas");
  txt(s, "Pegadas na areia", { x: 9.25, y: 1.65, w: 3.3, h: 0.8, fontSize: 20, bold: true, color: C.text2, margin: 0,
    valign: "middle", objectName: "título analogia" });
  txt(s, "Para saber o passo de alguém, não precisa ver a pessoa: mede de uma pegada até a próxima igual. " +
    "A voz também repete o mesmo desenho, como pegadas.", { x: 8.25, y: 2.6, w: 4.3, h: 1.3, fontSize: 14, color: C.text1,
    margin: 0, valign: "top", objectName: "texto analogia" });
  s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: 8.25, y: 3.95, w: 4.25, h: 2.05, rectRadius: 0.1,
    fill: { color: C.background1 }, line: { color: C.background1 }, objectName: "como ler" });
  txt(s, [
    { text: "COMO LER O GRÁFICO", options: { bold: true, color: C.accent4, fontSize: 11, charSpacing: 1, breakLine: true } },
    { text: "→ Para a direita: ", options: { bold: true, color: C.text2 } },
    { text: "quanto a cópia foi atrasada (em amostras).", options: { color: C.text1, breakLine: true } },
    { text: "↑ Para cima: ", options: { bold: true, color: C.text2 } },
    { text: "quanto ficou parecida. 1 = igual, 0 = nada a ver, −1 = ao contrário.", options: { color: C.text1, breakLine: true } },
    { text: "O pico mais alto depois do zero é a próxima pegada.", options: { bold: true, color: C.accent3 } },
  ], { x: 8.4, y: 4.02, w: 3.95, h: 1.9, fontSize: 13, margin: 0, valign: "top", paraSpaceAfter: 3, objectName: "texto como ler" });
  txt(s, "Ideia: em vez de provar a receita, medir de quanto em quanto tempo a onda se repete.", { x: 0.6, y: 6.35, w: 12.1,
    h: 0.45, fontSize: 16, bold: true, color: C.accent3, margin: 0, objectName: "ideia" });
  s.addNotes("Slide de intuição. Autocorrelação: r[τ] compara o sinal com ele mesmo atrasado τ amostras. " +
    "Em τ = 0 vale 1 (idêntico). Os picos menores vêm dos harmônicos; o pico mais alto dentro da faixa de voz " +
    "é o período fundamental.");

  // ----- C. autocorrelacao: passo a passo -----
  s = pres.addSlide({ masterName: "CONTEUDO", sectionTitle: "Processamento de sinais" });
  s.addText("Passo a passo 3: o tom real é " + nf(ac.f0, 0) + " Hz", { placeholder: "title" });
  etiquetaEng(s);
  const periodoMs = (1000 * (ac.tau + ac.d_tau)) / onda.sr;
  quadrinhos(s, [
    [I.copias, "Copiar e deslizar", "Atrasa a cópia aos poucos e mede o quanto ficou parecida.",
      "Duas folhas transparentes com as mesmas pegadas, uma deslizando sobre a outra.", "r[τ] = Σ x[n]·x[n+τ]"],
    [I.chave, "Procurar no lugar certo", "Só olha atrasos de " + ac.tau_min + " a " + ac.tau_max + " (vozes de 70 a 400 Hz).",
      "Procurar a chave só nos bolsos, e não na casa inteira.", "τ de " + ac.tau_min + " a " + ac.tau_max],
    [I.pegadas, "Achar o encaixe", "Mais parecido em " + ac.tau + " amostras: r = " + nf(ac.r_tau, 2) + ", perto de 1.",
      "Achar a distância exata de uma pegada até a próxima.", "τ = " + nf(ac.tau + ac.d_tau, 1)],
    [I.ventilador, "Contar as voltas", nf(ac.tau + ac.d_tau, 1) + " ÷ 22.050 = " + nf(periodoMs, 1) + " ms por volta = " +
      nf(ac.f0, 0) + " por segundo.",
      "Um ventilador que dá " + nf(ac.f0, 0) + " voltas por segundo.", "f₀ = 22.050 ÷ " + nf(ac.tau + ac.d_tau, 1)],
  ], C.accent3);
  faixa(s, [
    { text: "Mistério resolvido: ", options: { bold: true, color: C.accent1 } },
    { text: nf(fft.f_pico, 0) + " ÷ " + nf(ac.f0, 0) + " ≈ " + Math.round(razao) +
      ". O pico da FFT era o " + Math.round(razao) + "º harmônico: num coral, um cantor gritou mais alto que a nota.",
      options: { color: C.background1 } },
  ]);
  s.addNotes("Versão técnica: autocorrelação normalizada r[τ] = Σ x[n]·x[n+τ] / r[0]; busca em τ ∈ [fs/400, fs/70] = [" +
    ac.tau_min + ", " + ac.tau_max + "]; maior pico em τ = " + ac.tau + " (r = " + nf(ac.r_tau, 2) +
    "), refinado por interpolação parabólica para " + nf(ac.tau + ac.d_tau, 2) + "; f₀ = fs/τ = " + nf(ac.f0, 1) +
    " Hz, tom típico de voz masculina. Precisão ≈ fs/τ² ≈ " + nf(onda.sr / ac.tau ** 2, 2) + " Hz contra ±" +
    nf(fft.df / 2, 1) + " Hz de um bin da FFT. Conferência: " + nf(fft.f_pico, 1) + " / " + nf(ac.f0, 1) + " = " +
    nf(razao, 2) + ": o pico da FFT era o " + Math.round(razao) + "º harmônico, reforçado pela ressonância da vogal " +
    "(formante). É o erro de harmônico clássico dos detectores de pitch.");

  // ================= 7. secao discreta =================
  pres.addSection({ title: "Matemática discreta" });
  s = pres.addSlide({ masterName: "SECAO", sectionTitle: "Matemática discreta" });
  s.addText("Parte 2: o Zeus joga xadrez", { placeholder: "title" });
  s.addText("Matemática discreta: contar, combinar e cortar caminhos", { placeholder: "body" });
  s.addImage({ data: I.cavaloG, x: 9.6, y: 1.9, w: 2.8, h: 2.8, objectName: "cavalo" });
  rosto(s, 9.4, 5.0, 3.2, "calculando...", "Zeus pensando");
  s.addNotes("Mudança de assunto. Quando é a vez do Zeus, a tela do robô mostra 'calculando...' e o nome do celular adversário. " +
    "O que ele calcula? Uma árvore de possibilidades, e aí entra a matemática discreta: tudo ali é contável.");

  // ================= 8. arvore explode =================
  s = pres.addSlide({ masterName: "CONTEUDO", sectionTitle: "Matemática discreta" });
  s.addText("Cada lance multiplica as possibilidades por ~35", { placeholder: "title" });
  const prof = [1, 2, 3, 4, 5, 6];
  // numero no rotulo da categoria: o formato do rotulo de dados segue o idioma do Office (virgula em ingles)
  const ptBR = (n) => n.toLocaleString("pt-BR");
  s.addChart(pres.charts.BAR, [{ name: "posições",
    labels: prof.map((d) => ptBR(35 ** d) + "\n" + d + " lance" + (d > 1 ? "s" : "")),
    values: prof.map((d) => 35 ** d) }], {
    x: 0.6, y: 1.45, w: 7.6, h: 5.3, barDir: "col", chartColors: [HEX.accent4], valAxisLogScaleBase: 10,
    showValue: false, showTitle: true, title: "Posições possíveis = 35 elevado a n (escala log)",
    titleFontSize: 14, titleColor: HEX.dk2, titleFontFace: "+mn-lt", showLegend: false, valAxisHidden: true,
    valGridLine: { style: "none" }, catGridLine: { style: "none" }, catAxisLabelColor: HEX.accent6,
    catAxisLabelFontSize: 12, catAxisLabelFontFace: "+mn-lt", barGapWidthPct: 45, objectName: "explosão combinatória",
  });
  const fatos = [
    ["35", "lances legais, em média, numa posição de meio-jogo", C.accent1],
    ["1,8 bilhão", "posições olhando só 6 lances à frente", C.accent3],
    ["10¹²⁰", "partidas possíveis (número de Shannon), mais que átomos no universo", C.accent4],
  ];
  fatos.forEach(([n, l, cor], i) => {
    const y = 1.55 + i * 1.75;
    s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: 8.6, y, w: 4.15, h: 1.5, rectRadius: 0.12,
      fill: { color: C.background2 }, line: { color: C.background2 }, objectName: "fato " + (i + 1) });
    s.addShape(pres.shapes.OVAL, { x: 8.85, y: y + 0.25, w: 0.35, h: 0.35, fill: { color: cor }, line: { color: cor },
      objectName: "bolinha fato " + (i + 1) });
    txt(s, n, { x: 9.35, y: y + 0.1, w: 3.25, h: 0.65, fontSize: 28, bold: true, color: C.text2, margin: 0,
      valign: "middle", objectName: "valor fato " + (i + 1) });
    txt(s, l, { x: 9.35, y: y + 0.75, w: 3.25, h: 0.65, fontSize: 13, color: C.text1, margin: 0, valign: "top",
      objectName: "legenda fato " + (i + 1) });
  });
  s.addNotes("Princípio multiplicativo: se cada posição tem ~35 lances, n lances à frente dão 35^n posições. " +
    "O gráfico está em escala logarítmica, senão as barras da esquerda nem apareceriam. " +
    "Pergunta para a plateia: quantos anos o Zeus levaria para olhar tudo? Resposta: mais que a idade do universo. Então ele precisa cortar caminho.");

  // ================= 9. alfa-beta =================
  s = pres.addSlide({ masterName: "CONTEUDO", sectionTitle: "Matemática discreta" });
  s.addText("A tesoura do Zeus: poda alfa-beta", { placeholder: "title" });
  const raizX = 3.55, folhas = [[3, 12, 8], [2, 4, 6], [14, 5, 2]], mins = [3, 2, 2];
  const podada = (g, k) => g === 1 && k > 0;
  const nos = (x, y, w, h, t, fill, cor, nome, oval) => txt(s, t, { x, y, w, h, shape: oval ? pres.shapes.OVAL : pres.shapes.ROUNDED_RECTANGLE,
    rectRadius: 0.1, fill: { color: fill }, line: { color: fill }, color: cor, fontSize: 16, bold: true, align: "center",
    valign: "middle", margin: 0, objectName: nome });
  const linha = (x1, y1, x2, y2, line, nome) => s.addShape(pres.shapes.LINE, { x: Math.min(x1, x2), y: y1,
    w: Math.max(Math.abs(x2 - x1), 0.001), h: y2 - y1, flipH: x2 < x1, line, objectName: nome });
  for (let g = 0; g < 3; g++) {
    const gx = 0.75 + g * 2.45 + 0.6;
    linha(raizX + 0.4, 2.2, gx + 0.4, 3.15, { color: C.accent6, width: 1.5 }, "galho raiz " + (g + 1));
    for (let k = 0; k < 3; k++) {
      const fx = 0.75 + g * 2.45 + k * 0.75 + 0.05;
      const p = podada(g, k);
      linha(gx + 0.4, 3.75, fx + 0.3, 4.6, { color: p ? C.background2 : C.accent6, width: 1.5, dashType: p ? "dash" : "solid" },
        "galho folha " + (g * 3 + k + 1));
      nos(fx, 4.6, 0.6, 0.6, String(folhas[g][k]), p ? C.background2 : C.background1, p ? C.accent6 : C.text2,
        "folha " + (g * 3 + k + 1), false);
      if (!p) s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: fx, y: 4.6, w: 0.6, h: 0.6, rectRadius: 0.1,
        fill: { type: "none" }, line: { color: C.accent6, width: 1 }, objectName: "borda folha " + (g * 3 + k + 1) });
    }
    nos(gx, 3.15, 0.8, 0.6, String(mins[g]), C.accent2, C.text2, "mínimo " + (g + 1), false);
  }
  s.addImage({ data: I.tesoura, x: 3.95, y: 3.95, w: 0.5, h: 0.5, objectName: "tesoura" });
  nos(raizX, 1.6, 0.8, 0.6, "3", C.accent1, C.text2, "raiz", false);
  [[C.accent1, "Zeus escolhe o MAIOR"], [C.accent2, "Você responde com o MENOR"]].forEach(([cor, l], i) => {
    const x = 0.75 + i * 3.4;
    s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x, y: 5.5, w: 0.35, h: 0.35, rectRadius: 0.06, fill: { color: cor },
      line: { color: cor }, objectName: "legenda cor " + (i + 1) });
    txt(s, l, { x: x + 0.5, y: 5.45, w: 2.8, h: 0.45, fontSize: 15, color: C.text1, margin: 0, valign: "middle",
      objectName: "legenda árvore " + (i + 1) });
  });
  txt(s, "No 2º galho, o 2 já é pior que 3: os outros dois nem são olhados.", { x: 0.75, y: 6.05, w: 7.4, h: 0.5,
    fontSize: 15, bold: true, color: C.text2, margin: 0, valign: "top", objectName: "explicação poda" });
  s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: 8.4, y: 1.55, w: 4.35, h: 4.95, rectRadius: 0.15,
    fill: { color: C.text2 }, line: { color: C.text2 }, objectName: "cartão conta poda" });
  txt(s, "4 lances à frente", { x: 8.7, y: 1.75, w: 3.8, h: 0.45, fontSize: 16, bold: true, color: C.accent1, margin: 0,
    objectName: "título conta poda" });
  txt(s, "Sem poda: 35⁴", { x: 8.7, y: 2.3, w: 3.8, h: 0.4, fontSize: 15, color: C.background1, margin: 0,
    objectName: "sem poda rótulo" });
  txt(s, "1.500.625", { x: 8.7, y: 2.7, w: 3.8, h: 0.65, fontSize: 32, bold: true, color: C.background1, margin: 0,
    objectName: "sem poda valor" });
  txt(s, "Com poda (melhor caso): 35² + 35² − 1", { x: 8.7, y: 3.55, w: 3.8, h: 0.4, fontSize: 15, color: C.background1, margin: 0,
    objectName: "com poda rótulo" });
  txt(s, "2.449", { x: 8.7, y: 3.95, w: 3.8, h: 0.65, fontSize: 32, bold: true, color: C.accent2, margin: 0,
    objectName: "com poda valor" });
  txt(s, "≈ 613× menos trabalho", { x: 8.7, y: 4.85, w: 3.8, h: 0.55, fontSize: 22, bold: true, color: C.accent1, margin: 0,
    objectName: "ganho poda" });
  txt(s, "É assim que o Stockfish pensa em ~1,5 s", { x: 8.7, y: 5.5, w: 3.8, h: 0.7, fontSize: 14, color: C.accent6,
    margin: 0, objectName: "nota stockfish" });
  s.addNotes("Minimax: o Zeus (amarelo) quer o maior valor; ele supõe que você (ciano) vai escolher o pior para ele, o menor. " +
    "Primeiro galho: mínimo 3. Segundo galho: a primeira folha já dá 2, que é pior que 3. Não importa o resto, então ele corta (tesoura). " +
    "Fórmula do melhor caso (Knuth e Moore): b^teto(d/2) + b^piso(d/2) − 1. Com b = 35 e d = 4 dá 2.449 em vez de 1,5 milhão.");

  // ================= 10. desafio cavalo =================
  s = pres.addSlide({ masterName: "CONTEUDO", sectionTitle: "Matemática discreta" });
  s.addText("Desafio: quantos saltos de cavalo existem?", { placeholder: "title" });
  const salt = [[1, 2], [2, 1], [-1, 2], [-2, 1], [1, -2], [2, -1], [-1, -2], [-2, -1]];
  const grau = (r, c) => salt.filter(([a, b]) => r + a >= 0 && r + a < 8 && c + b >= 0 && c + b < 8).length;
  const corGrau = { 2: C.background2, 3: C.accent2, 4: C.accent5, 6: C.accent1, 8: C.accent3 };
  const cel = 0.62, bx = 0.75, by = 1.5;
  let soma = 0;
  for (let r = 0; r < 8; r++) for (let c = 0; c < 8; c++) {
    const g = grau(r, c); soma += g;
    txt(s, String(g), { x: bx + c * cel, y: by + r * cel, w: cel, h: cel, shape: pres.shapes.RECTANGLE,
      fill: { color: corGrau[g] }, line: { color: C.background1, width: 1.5 }, color: C.text2, fontSize: 15, bold: true,
      align: "center", valign: "middle", margin: 0, objectName: `casa ${"abcdefgh"[c]}${8 - r}` });
  }
  if (soma !== 336) throw new Error("soma dos graus " + soma);
  s.addImage({ data: I.cavalo, x: bx + 3 * cel + 0.11, y: by + 3 * cel + 0.06, w: 0.4, h: 0.4, objectName: "cavalo no centro" });
  const lx = 6.2;
  txt(s, "Cada casa mostra para quantas casas o cavalo pode pular dali.", { x: lx, y: 1.5, w: 6.5, h: 0.75,
    fontSize: 16, color: C.text1, margin: 0, valign: "top", objectName: "explicação desafio" });
  txt(s, "Chute antes de virar a resposta!", { x: lx, y: 2.3, w: 6.5, h: 0.5, fontSize: 20, bold: true,
    color: C.accent3, margin: 0, objectName: "chamada quiz" });
  s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: lx, y: 3.25, w: 6.5, h: 3.1, rectRadius: 0.15,
    fill: { color: C.text2 }, line: { color: C.text2 }, objectName: "cartão resposta" });
  txt(s, "Some todos os números:", { x: lx + 0.35, y: 3.45, w: 5.8, h: 0.45, fontSize: 16, color: C.accent2, margin: 0,
    objectName: "passo 1 resposta" });
  txt(s, "336 saltos", { x: lx + 0.35, y: 3.9, w: 5.8, h: 0.8, fontSize: 40, bold: true, color: C.background1, margin: 0,
    objectName: "total saltos" });
  txt(s, "Cada caminho A↔B foi contado duas vezes (de A e de B). Lema do aperto de mão: soma dos graus = 2 × arestas.",
    { x: lx + 0.35, y: 4.75, w: 5.8, h: 0.8, fontSize: 15, color: C.background1, margin: 0, valign: "top",
      objectName: "lema do aperto de mão" });
  txt(s, "336 ÷ 2 = 168 caminhos diferentes", { x: lx + 0.35, y: 5.6, w: 5.8, h: 0.55, fontSize: 22, bold: true,
    color: C.accent1, margin: 0, objectName: "resposta final" });
  s.addNotes("Momento quiz: antes de revelar o cartão escuro, deixe a plateia chutar. " +
    "O tabuleiro vira um GRAFO: casas são vértices, saltos são arestas. O número em cada casa é o grau. " +
    "Soma dos graus = 336; como cada aresta tem duas pontas, existem 168 arestas. " +
    "O gerador de lances do Stockfish faz exatamente esse tipo de conta, milhões de vezes por segundo.");

  // ================= 11. encerramento =================
  pres.addSection({ title: "Encerramento" });
  s = pres.addSlide({ masterName: "SECAO", sectionTitle: "Encerramento" });
  s.addText("Ouvir, pensar, falar", { placeholder: "title" });
  s.addText("Perguntas? O Zeus responde (se você começar com “Zeus”).", { placeholder: "body" });
  const fim = [["Ouvir", "sinais: amostras, palpites, filtro", C.accent2],
    ["Pensar", "discreta: árvores, poda, grafos", C.accent4],
    ["Falar", "sinais: onda + boca no ritmo", C.accent3]];
  fim.forEach(([t, d, cor], i) => {
    const x = 0.7 + i * 4.05;
    s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x, y: 5.75, w: 3.8, h: 1.0, rectRadius: 0.12, fill: { color: "232845" },
      line: { color: "232845" }, objectName: "cartão " + t });
    s.addShape(pres.shapes.OVAL, { x: x + 0.25, y: 5.95, w: 0.3, h: 0.3, fill: { color: cor }, line: { color: cor },
      objectName: "bolinha " + t });
    txt(s, t, { x: x + 0.7, y: 5.85, w: 2.9, h: 0.45, fontSize: 18, bold: true, color: C.background1, margin: 0,
      valign: "middle", objectName: "título " + t });
    txt(s, d, { x: x + 0.7, y: 6.27, w: 2.95, h: 0.4, fontSize: 13, color: C.accent6, margin: 0, valign: "top",
      objectName: "resumo " + t });
  });
  rosto(s, 9.2, 2.6, 3.4, "Obrigado!", "Zeus final");
  s.addNotes("Resumo em uma frase: o Zeus ouve e fala com processamento de sinais, e pensa com matemática discreta. " +
    "Se der, faça a demonstração ao vivo: 'Zeus, vamos jogar xadrez' e mostre o painel no notebook com o 'calculando...' no robô.");

  const arq = process.argv[2] || "Zeus_Sinais_e_Discreta.pptx"; // outro nome quando o PowerPoint esta com o arquivo aberto
  await pres.writeFile({ fileName: arq });
  await applyTheme(arq, THEME);
  console.log("ok", arq);
})().catch((e) => { console.error(e); process.exit(1); });
