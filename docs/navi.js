// Navigation und Hell/Dunkel für alle HTML-Seiten in docs/.
// Neue Seiten in SEITEN eintragen, neue Übergaben oben in UEBERGABEN.
// Ältere Übergaben (bis 08.10.2026) liegen noch als .md in docs/.
(function () {
  const SEITEN = [];
  const UEBERGABEN = [
    ["2026-10-21-statusnotiz.html", "21.10.2026"],
  ];
  const r = document.documentElement, schluessel = "nerdminer-thema";
  try { const t = localStorage.getItem(schluessel); if (t) r.dataset.theme = t; } catch (e) {}
  document.addEventListener("DOMContentLoaded", () => {
    const nav = document.getElementById("navi");
    if (!nav) return;
    const hier = decodeURIComponent(location.pathname.split("/").pop() || "index.html");
    const link = (href, text, klasse) => {
      const a = document.createElement("a");
      a.href = href; a.textContent = text; a.className = klasse;
      return a;
    };
    const titel = document.createElement("div");
    titel.className = "titel"; titel.textContent = "NerdMiner awiEdition";
    nav.append(titel);
    for (const [datei, text] of SEITEN) {
      nav.append(link(datei, text, "k" + (datei === hier ? " aktiv" : "")));
      if (datei === hier) {
        for (const h of document.querySelectorAll("main h2[id]")) nav.append(link("#" + h.id, h.textContent, "u"));
      }
    }
    const gruppe = document.createElement("div");
    gruppe.className = "gruppe"; gruppe.textContent = "Übergaben";
    nav.append(gruppe);
    for (const [datei, text] of UEBERGABEN) {
      nav.append(link(datei, text, "k klein" + (datei === hier ? " aktiv" : "")));
      if (datei === hier) {
        for (const h of document.querySelectorAll("main h2[id]")) nav.append(link("#" + h.id, h.textContent, "u"));
      }
    }
    const knopf = document.createElement("button");
    knopf.className = "themaknopf"; knopf.type = "button"; knopf.textContent = "Hell / Dunkel";
    knopf.addEventListener("click", () => {
      const dunkel = r.dataset.theme ? r.dataset.theme === "dark" : matchMedia("(prefers-color-scheme: dark)").matches;
      r.dataset.theme = dunkel ? "light" : "dark";
      try { localStorage.setItem(schluessel, r.dataset.theme); } catch (e) {}
    });
    nav.append(knopf);
  });
})();
