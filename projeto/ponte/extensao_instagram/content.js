// Roda na aba do Instagram, no Chrome normal (logado). Executa as tarefas que a ponte do
// Zeus manda: "rascunho" (Criar > foto > Avancar > Avancar > legenda, PARA antes de
// Compartilhar), "publicar" (clica em Compartilhar) e "cancelar" (fecha e descarta).
(() => {
  const dormir = ms => new Promise(r => setTimeout(r, ms));
  const visivel = el => { const r = el.getBoundingClientRect(); return r.width > 0 && r.height > 0; };

  // textos em portugues e ingles (o Instagram segue o idioma da conta)
  const CRIAR = ['Novo post', 'Nova publicação', 'New post', 'Criar', 'Create'];
  const PUBLICACAO = ['Publicação', 'Post'];
  const AVANCAR = ['Avançar', 'Next'];
  const COMPARTILHAR = ['Compartilhar', 'Share'];
  const DESCARTAR = ['Descartar', 'Discard'];
  const FECHAR = ['Fechar', 'Close'];
  const CONCLUIR = ['Concluir', 'Done'];
  // "Seu post foi compartilhado." (o Instagram ja usou "publicação ... compartilhada")
  const COMPARTILHADO = /foi compartilhad[oa]|post compartilhado|has been shared|post shared/i;

  function logado() {
    return !!document.querySelector('svg[aria-label="Página inicial"], svg[aria-label="Home"], a[href="/direct/inbox/"]')
      && !document.querySelector('input[type="password"]');
  }

  function dialogo() {
    const ds = [...document.querySelectorAll('div[role="dialog"]')].filter(visivel);
    return ds[ds.length - 1] || null;
  }

  // botao, link ou icone com o texto (ou aria-label) exato
  function achar(textos, raiz) {
    if (!raiz) return null;
    const cand = [...raiz.querySelectorAll('button, [role="button"], a, [role="link"], [role="menuitem"], div[tabindex]')];
    for (const t of textos) {
      for (const el of cand) {
        const tx = (el.innerText || '').trim();
        if ((tx === t || el.getAttribute('aria-label') === t) && visivel(el)) return el;
      }
      const svg = raiz.querySelector(`svg[aria-label="${t}"]`);
      if (svg && visivel(svg)) return svg.closest('a, button, [role="button"], [role="link"], div[tabindex]') || svg;
    }
    return null;
  }

  async function esperar(fn, ms, passo = 300) {
    const fim = Date.now() + ms;
    while (Date.now() < fim) { const v = fn(); if (v) return v; await dormir(passo); }
    return null;
  }

  // soNoDialogo: o feed tambem tem "Avançar" (stories) e "Compartilhar" (enviar post de outra
  // pessoa); sem a janela de criar aberta, espera em vez de procurar na pagina toda
  async function clicar(textos, ms, soNoDialogo) {
    const el = await esperar(() => {
      if (!soNoDialogo) return achar(textos, document);
      const d = dialogo();
      return d ? achar(textos, d) : null;
    }, ms);
    if (!el) throw new Error(`não achei o botão "${textos[0]}" no Instagram`);
    el.click();
    return el;
  }

  async function cancelar() {
    for (let i = 0; i < 3 && dialogo(); i++) {
      const x = achar(FECHAR, dialogo()) || achar(FECHAR, document);
      if (x) x.click(); else document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true }));
      await dormir(700);
      const d = achar(DESCARTAR, document);
      if (d) { d.click(); await dormir(700); }
    }
    return { ok: true };
  }

  async function rascunho(job, foto) {
    if (!logado()) throw new Error('o Instagram não está logado neste Chrome: entre na sua conta');
    await cancelar();
    await clicar(CRIAR, 10000);
    await dormir(1200);
    const sub = achar(PUBLICACAO, document);           // menu novo: Criar > Publicacao
    if (sub && !dialogo()) { sub.click(); await dormir(1200); }
    const entrada = await esperar(() => (dialogo() || document).querySelector('input[type="file"]'), 15000);
    if (!entrada) throw new Error('a janela "Criar nova publicação" não abriu');
    // a foto entra direto no campo de arquivo, sem a janela de escolher arquivo do Windows
    const blob = await (await fetch(foto)).blob();
    const dt = new DataTransfer();
    dt.items.add(new File([blob], job.nome || 'zeus.jpg', { type: 'image/jpeg' }));
    entrada.files = dt.files;
    entrada.dispatchEvent(new Event('change', { bubbles: true }));
    await clicar(AVANCAR, 20000, true);                // recorte
    await dormir(1500);
    await clicar(AVANCAR, 20000, true);                // filtros
    await dormir(1500);
    if (job.legenda) {
      const ed = await esperar(() => { const d = dialogo(); return d && d.querySelector('div[contenteditable="true"]'); }, 15000);
      if (!ed) throw new Error('não achei a caixa de legenda');
      ed.focus();
      document.execCommand('insertText', false, job.legenda);   // o editor do Instagram aceita assim
    }
    return { ok: true };
  }

  async function publicar() {
    if (!dialogo()) throw new Error('a publicação preparada não está mais aberta no Instagram');
    await clicar(COMPARTILHAR, 10000, true);
    const ok = await esperar(() => COMPARTILHADO.test(document.body.innerText), 90000, 1000);
    if (!ok) throw new Error('cliquei em Compartilhar, mas não vi a confirmação do Instagram');
    // fecha a janela "Post compartilhado" pelo botao Concluir
    await dormir(800);
    const concluir = await esperar(() => achar(CONCLUIR, dialogo()) || achar(CONCLUIR, document), 8000);
    if (concluir) { concluir.click(); await dormir(800); }
    return { ok: true, publicado: true, concluido: !!concluir };
  }

  // so leitura: os nomes que a pagina mostra, para ajustar os textos acima quando o Instagram muda
  function diagnostico() {
    const nomes = sel => [...new Set([...document.querySelectorAll(sel)].filter(visivel)
      .map(e => (e.getAttribute('aria-label') || e.innerText || '').trim().slice(0, 40)).filter(Boolean))];
    return {
      ok: true, url: location.href, idioma: document.documentElement.lang, logado: logado(),
      icones: nomes('svg[aria-label]'), links: nomes('a, [role="link"]').slice(0, 40),
      botoes: nomes('button, [role="button"]').slice(0, 40),
      dialogo: dialogo() ? dialogo().innerText.slice(0, 300) : null,
    };
  }

  let ocupado = false;
  async function ciclo() {
    if (ocupado) return;
    let r;
    try { r = await chrome.runtime.sendMessage({ tipo: 'ping', logado: logado() }); } catch (e) { return; }
    if (!r || !r.job) return;
    ocupado = true;
    let res;
    try {
      const a = r.job.acao;
      res = a === 'rascunho' ? await rascunho(r.job, r.foto)
          : a === 'publicar' ? await publicar()
          : a === 'cancelar' ? await cancelar()
          : a === 'diagnostico' ? diagnostico()
          : { ok: true };                                // "focar": a aba ja veio para a frente
    } catch (e) {
      res = { ok: false, erro: String(e.message || e) };
    }
    try { await chrome.runtime.sendMessage({ tipo: 'resultado', id: r.job.id, ...res }); } catch (e) {}
    ocupado = false;
  }
  setInterval(ciclo, 1500);
  ciclo();
})();
