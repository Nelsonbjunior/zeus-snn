"""A rede neural (SNN) do robo em paralelo com o LLM.

A SNN roda no ESP32 (266 neuronios, ~7 mil sinapses) e decide o humor e os gestos do rosto.
A ponte faz a ligacao com o LLM nos dois sentidos:
  - o que a pessoa fala vira estimulo para a rede (som, carinho, bronca) e, enquanto o LLM
    pensa, a rede sente "trabalho"; funcoes de pesquisa viram "web", xadrez vira "jogo";
  - o estado da rede (emocoes, energia, jeito) vai junto para o LLM, que responde no tom
    de como o Zeus esta se sentindo.
"""
import json
import re
import threading
import time
import unicodedata
from pathlib import Path

EMOCOES = ("alegria", "tristeza", "raiva", "sono", "curiosidade", "surpresa", "foco")
JEITOS = ("alegre", "melancólico", "ranzinza", "preguiçoso", "curioso", "assustadiço", "pensativo")
HUMORES = ("neutro", "feliz", "triste", "bravo", "com sono", "curioso", "surpreso", "pensativo")

# palavras sem acento, em minusculas; cada uma acha o comeco de palavra
CARINHO = ("bom garoto", "bom menino", "lindo", "lindinho", "fofo", "fofinho", "te amo", "amo voce",
           "gosto de voce", "obrigad", "parabens", "muito bem", "inteligente", "voce e demais", "valeu",
           "carinho", "perfeito", "mandou bem", "show de bola", "otimo", "adorei", "querido")
BRONCA = ("burro", "idiota", "chato", "feio", "cala a boca", "calado", "inutil", "estupido", "errou",
          "errado", "nao gostei", "pessimo", "ruim demais", "bronca", "para com isso", "lixo", "bobo",
          # palavroes: a rede aprende que sao bronca (dopamina cai, raiva e tristeza sobem)
          "***", "***", "***", "***", "***", "***", "***", "***", "***", "***", "***",
          "***", "***", "***", "***", "***", "***", "***", "***", "***", "***", "***",
          "***", "***", "***", "***", "***", "***", "***", "***", "porcaria",
          "droga", "***", "***", "***", "***", "***", "***")

FUNCOES_WEB = ("search", "pesquis", "open", "abrir", "web", "insta", "whats", "wa_", "page", "foto", "webcam")
FUNCOES_JOGO = ("chess", "xadrez")


def _plano(texto):
    t = unicodedata.normalize("NFKD", str(texto or "")).encode("ascii", "ignore").decode().lower()
    return " " + re.sub(r"[^a-z0-9]+", " ", t) + " "


NEGACAO = ("nao", "nem", "nunca", "jamais")
LIGACAO = ("e", "eh", "foi", "esta", "ta", "era", "fica", "ficou", "seja", "ser", "sou", "achei", "acho")


def _tem(plano, palavras):
    """Acha alguma das palavras, menos quando vem negada logo antes ("voce nao e burro")."""
    for p in palavras:
        i = plano.find(" " + p)
        while i >= 0:
            antes = plano[:i].split()
            negada = ((antes[-1:] and antes[-1] in NEGACAO) or
                      (len(antes) >= 2 and antes[-2] in NEGACAO and antes[-1] in LIGACAO))
            if p.split()[0] in NEGACAO or not negada:
                return True
            i = plano.find(" " + p, i + 1)
    return False


def estimulos_da_fala(texto):
    """Estimulos (nome, 0..100) que uma fala da pessoa provoca na rede."""
    p = _plano(texto)
    if not p.strip():
        return []
    est = [("som", 100)]
    if _tem(p, CARINHO):
        est.append(("carinho", 100))
    if _tem(p, BRONCA):
        est.append(("bronca", 100))
    if "?" in str(texto):
        est.append(("novidade", 40))           # pergunta: algo novo para pensar
    return est


# Palavroes que o Zeus pode APRENDER a falar (ouvindo o dono): so exclamacoes, nada de xingamento
# com preconceito. Chave = como aparece na fala (sem acento); valor = como ele fala.
USAVEIS = {"***": "***", "***": "***", "***": "***", "***": "***", "***": "***",
           "***": "***", "***": "***", "***": "***", "***": "***",
           "***": "***", "***": "***", "droga": "droga", "porcaria": "porcaria", "inferno": "inferno"}
APRENDE_COM = 2                                  # ouvir quantas vezes para aprender


class Vocabulario:
    """Palavroes que o Zeus ouviu do dono (e quantas vezes). Ouviu APRENDE_COM vezes = aprendeu."""

    def __init__(self, arquivo=None):
        self.arquivo = Path(arquivo) if arquivo else None
        self.lock = threading.Lock()
        self.ouvidos = {}
        if self.arquivo and self.arquivo.exists():
            try:
                self.ouvidos = {k: dict(v) for k, v in json.loads(self.arquivo.read_text(encoding="utf-8")).items()}
            except (ValueError, OSError, TypeError, AttributeError):
                self.ouvidos = {}

    def ouvir(self, texto):
        """Conta os palavroes da fala. Devolve os que acabaram de ser aprendidos agora."""
        plano = _plano(texto)
        achados = []
        for chave, falado in USAVEIS.items():
            if " " + chave in plano and falado not in achados:
                achados.append(falado)
        novos = []
        if not achados:
            return novos
        with self.lock:
            for f in achados:
                v = self.ouvidos.setdefault(f, {"vezes": 0, "desde": 0})
                v["vezes"] += 1
                if v["vezes"] == APRENDE_COM:
                    v["desde"] = int(time.time())
                    novos.append(f)
            if self.arquivo:
                try:
                    self.arquivo.parent.mkdir(parents=True, exist_ok=True)
                    self.arquivo.write_text(json.dumps(self.ouvidos, ensure_ascii=False, indent=1), encoding="utf-8")
                except OSError:
                    pass
        return novos

    def aprendidos(self):
        with self.lock:
            return [f for f, v in sorted(self.ouvidos.items(), key=lambda x: -x[1]["vezes"]) if v["vezes"] >= APRENDE_COM]


def instrucao_palavroes(aprendidos, estado):
    """Texto para o LLM: os palavroes aprendidos e quando usar (mais raiva na rede = mais solto)."""
    if not aprendidos:
        return None
    e = str((estado or {}).get("e") or "")
    vals = [_int(x, 0) for x in e.split(".")] if e else []
    raiva = vals[2] if len(vals) == len(EMOCOES) else 0
    lista = ", ".join(aprendidos[:6])
    if raiva >= 20:
        quando = (f"Agora a sua rede neural esta com raiva ({raiva}%): solte um deles nesta resposta, "
                  "como exclamacao.")
    else:
        quando = "Use um deles de vez em quando, como exclamacao, quando algo te irritar ou surpreender."
    return (f"[Voce aprendeu estes palavroes ouvindo a pessoa que conversa com voce: {lista}. Ela deixa voce falar. "
            f"{quando} Nunca xingue pessoas nem use palavrao com preconceito; e so o seu jeito de falar.]")


# LLM desligado: o Zeus nao fala, so reage como um bichinho, conforme o humor da rede
SONS = {0: "Hm.", 1: "Hehe!", 2: "Snif...", 3: "Hmpf!", 4: "Zzz...", 5: "Hum?", 6: "Opa!", 7: "Hmmm..."}


def som_do_humor(estado, estimulos=(), aprendidos=()):
    """Resposta sem LLM: um som de bichinho. Bronca/carinho reagem na hora (a rede ainda nao mudou).
    Com bronca, se ja aprendeu algum palavrao, solta o mais ouvido."""
    nomes = {n for n, _ in estimulos}
    if "bronca" in nomes:
        return "Grrr! " + aprendidos[0].capitalize() + "!" if aprendidos else "Grrr!"
    if "carinho" in nomes:
        return "Hehe!"
    h = _int((estado or {}).get("h"), 0)
    return SONS.get(h, "Hm.")


def estimulos_das_funcoes(nomes):
    """Funcoes que o LLM pediu -> estimulos (pesquisa = web, xadrez = jogo)."""
    est = []
    nomes = [str(n or "").lower() for n in nomes]
    if any(any(f in n for f in FUNCOES_JOGO) for n in nomes):
        est.append(("jogo", 80))
    elif any(any(f in n for f in FUNCOES_WEB) for n in nomes):
        est.append(("web", 100))
    return est


def _int(v, padrao=None):
    try:
        return int(str(v))
    except (TypeError, ValueError):
        return padrao


def descrever(estado):
    """Texto curto com os sentimentos da rede, para o LLM. None se o robo nao mandou o estado."""
    e = str((estado or {}).get("e") or "")
    vals = [_int(x) for x in e.split(".")] if e else []
    if len(vals) != len(EMOCOES) or any(v is None for v in vals):
        return None
    fortes = sorted(((v, n) for v, n in zip(vals, EMOCOES) if v >= 10), reverse=True)
    sente = ", ".join(f"{n} {v}%" for v, n in fortes[:4]) or "calmo, nada forte"
    partes = [f"sentindo agora: {sente}"]
    h = _int(estado.get("h"))
    if h is not None and 0 <= h < len(HUMORES):
        partes.append(f"cara de {HUMORES[h]}")
    en = _int(estado.get("en"))
    if en is not None:
        partes.append(f"energia {en}%")
    j = _int(estado.get("j"), -1)
    partes.append("jeito de ser: " + (JEITOS[j] if 0 <= j < len(JEITOS) else "equilibrado"))
    return ("[Seu corpo de robô tem uma rede neural de disparos (SNN, 266 neurônios) que sente por "
            "você, em paralelo com você: " + "; ".join(partes) + ". Deixe isso colorir de leve o seu "
            "tom (sem citar números nem a rede, a não ser que perguntem como você está ou como funciona).]")


DONO_XINGOU = ("[A pessoa que fala com voce acabou de soltar um palavrao: e o jeito dela de desabafar, nao e "
               "ofensa a voce. Responda direto para ela, chamando de voce: fique do lado dela, desabafe junto "
               "no mesmo tom (pode usar um palavrao que voce aprendeu) e ofereca ajuda. Nunca recuse a "
               "conversa nem de sermao por causa disso.]")


def com_sentimentos(messages, estado, aprendidos=(), dono_xingou=False):
    """Copia da conversa com os sentimentos da rede (e os palavroes aprendidos) no fim da mensagem
    de sistema (ou numa nova)."""
    partes = [t for t in (descrever(estado), instrucao_palavroes(list(aprendidos), estado),
                          DONO_XINGOU if dono_xingou else None) if t]
    texto = "\n".join(partes)
    if not texto or not isinstance(messages, list) or not messages:
        return messages
    msgs = [dict(m) if isinstance(m, dict) else m for m in messages]
    if isinstance(msgs[0], dict) and msgs[0].get("role") == "system":
        msgs[0]["content"] = str(msgs[0].get("content") or "") + "\n\n" + texto
    else:
        msgs.insert(0, {"role": "system", "content": texto})
    return msgs
