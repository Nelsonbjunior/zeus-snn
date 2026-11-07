"""Copia ponte/cerebro_snn.py para dados/cerebro_snn_limpo.py com os palavroes mascarados ("***"),
para o anexo da edicao limpa (LinkedIn) do documento. O codigo real nao muda."""
import ast
import re
import sys
from pathlib import Path

AQUI = Path(__file__).resolve().parent
ORIGEM = AQUI.parent.parent / "ponte" / "cerebro_snn.py"
DESTINO = AQUI / "dados" / "cerebro_snn_limpo.py"
LEVES = {"droga", "porcaria", "inferno", "burro", "idiota", "chato", "feio", "bobo", "lixo", "pessimo", "ruim demais"}

fonte = ORIGEM.read_text(encoding="utf-8")
arvore = ast.parse(fonte)
valores = {}
for no in arvore.body:
    if isinstance(no, ast.Assign) and isinstance(no.targets[0], ast.Name):
        try:
            valores[no.targets[0].id] = ast.literal_eval(no.value)
        except ValueError:
            pass

bronca = list(valores["BRONCA"])
fortes = set(bronca[bronca.index("porra"):])            # a partir do comentario "# palavroes" da lista
fortes |= set(valores["USAVEIS"]) | set(valores["USAVEIS"].values())
fortes -= LEVES

limpo = fonte
for p in sorted(fortes, key=len, reverse=True):
    limpo = re.sub(r'"%s"' % re.escape(p), '"***"', limpo)
DESTINO.write_text(limpo, encoding="utf-8")

sobra = [p for p in fortes if re.search(r"\b%s\b" % re.escape(p), limpo)]
if sobra:
    sys.exit("limpa_ponte: ainda aparecem " + ", ".join(sorted(sobra)))
print(f"limpa_ponte: {len(fortes)} palavras mascaradas -> {DESTINO.name}")
