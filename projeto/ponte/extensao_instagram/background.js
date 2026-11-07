// Fala com a ponte do Zeus (127.0.0.1:8770). A aba do Instagram pergunta por tarefas
// a cada 1,5 s ("ping"); aqui buscamos a tarefa na ponte e a foto, e devolvemos o resultado.
importScripts('config.js');   // gerado pela ponte: self.ZEUS = {ponte, token}

async function api(caminho, corpo) {
  const o = { headers: { 'X-Zeus-Token': self.ZEUS.token } };
  if (corpo) { o.method = 'POST'; o.headers['Content-Type'] = 'application/json'; o.body = JSON.stringify(corpo); }
  const r = await fetch(self.ZEUS.ponte + caminho, o);
  return r.json();
}

// a foto vai como data URL: mensagens entre extensao e pagina so levam JSON
async function fotoDataUrl() {
  const r = await fetch(self.ZEUS.ponte + '/foto/ultima?t=' + encodeURIComponent(self.ZEUS.token));
  if (!r.ok) throw new Error('a ponte não tem foto: tire uma foto com o celular antes');
  const bytes = new Uint8Array(await r.arrayBuffer());
  let s = '';
  for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
  return 'data:image/jpeg;base64,' + btoa(s);
}

chrome.runtime.onMessage.addListener((m, remetente, responder) => {
  (async () => {
    if (m.tipo === 'ping') {
      let job = null;
      try { job = (await api('/insta/job?logado=' + (m.logado ? 1 : 0))).job; }
      catch (e) { responder({}); return; }            // ponte desligada: tenta no proximo ping
      if (!job) { responder({}); return; }
      try {
        if (remetente.tab) {                            // traz a aba do Instagram para a frente
          chrome.tabs.update(remetente.tab.id, { active: true });
          chrome.windows.update(remetente.tab.windowId, { focused: true });
        }
        const foto = job.acao === 'rascunho' ? await fotoDataUrl() : null;
        responder({ job, foto });
      } catch (e) {
        await api('/insta/resultado', { id: job.id, ok: false, erro: String(e.message || e) }).catch(() => {});
        responder({});
      }
    } else if (m.tipo === 'resultado') {
      try { await api('/insta/resultado', m); } catch (e) {}
      responder({});
    }
  })();
  return true;   // resposta assincrona
});
