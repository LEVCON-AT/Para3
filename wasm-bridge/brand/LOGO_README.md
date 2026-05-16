# PARA·3 — Logo / Brand

Hochwertige, schriftunabhaengige Vektor-Marke. Lesbarkeit war die Vorgabe —
geprueft durch reales Rendern bis Favicon-Groesse auf beiden Themes.

## Dateien

- `para3-logo.svg` — Lockup (Marke + Wortmarke). Theme-adaptiv:
  Text/Marke nutzen `currentColor`, Akzent fix Burnt Orange `#d9622b`.
- `para3-mark.svg` — quadratische Marke allein (In-App, currentColor).
- `para3-icon-maskable.svg` — PWA-Install-Icon (512², voller Anschnitt,
  Inhalt in der maskable Safe-Zone, OBSIDIAN-Grund).
- `para3-logo-preview.html` — Beleg: beide Themes, gross + 96 px +
  Favicon + App-Icon-Kontext. Im Browser oeffnen zum Pruefen.

## Einbau (fuer Claude Code)

1. **Wortmarke im UI ersetzen.** In `wasm-bridge/para3-responsive.html`
   den aktuellen „PARA·3"-Schriftzug im Header durch ein inline-`<svg>`
   bzw. `<img src="brand/para3-logo.svg">` ersetzen. Hoehe ~32–40 px, der
   `currentColor`-Mechanismus uebernimmt automatisch OBSIDIAN/BONE
   (Header-`color` setzen, fertig).
2. **PWA-Icon.** Im `manifest.webmanifest`:
   `"icons":[{"src":"brand/para3-icon-maskable.svg","sizes":"any",
   "type":"image/svg+xml","purpose":"any maskable"}]`.
   Optional zusaetzlich 192/512-PNG aus dem SVG rendern, falls Zielgeraete
   SVG-Icons nicht akzeptieren.
3. **Favicon.** `<link rel="icon" href="brand/para3-mark.svg">`.

Keine Schrift-Abhaengigkeit, keine externen Assets. Akzentfarbe an einer
Stelle (`#d9622b`) — bei Bedarf dort zentral aendern.
