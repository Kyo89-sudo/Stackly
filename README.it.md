<p align="center">
  <img src="res/stackly_256.png" width="128" height="128" alt="Icona di Stackly">
</p>

<h1 align="center">Stackly</h1>

<p align="center">
  Interfaccia per <b>yt-dlp</b> ispirata a Stacher, ricostruita da zero.<br>
  Win32 C++17 nativo: un solo <code>.exe</code>, nessun runtime da installare.
</p>

<p align="center">
  <a href="https://github.com/Kyo89-sudo/Stackly/releases/latest"><b>Scarica l'ultima versione</b></a>
  &nbsp;·&nbsp;
  <a href="CHANGELOG.md">Changelog tecnico</a>
  &nbsp;·&nbsp;
  <a href="https://github.com/Kyo89-sudo/Stackly/issues">Segnala un problema</a>
  &nbsp;·&nbsp;
  <a href="README.md">English</a>
</p>

<p align="center">
  <img src="docs/stackly-window.png" width="820" alt="Stackly, finestra principale con il tema scuro">
</p>

## Cosa fa

- **Scarica video, audio e sottotitoli** da YouTube, RaiPlay, Mediaset, Vimeo, SoundCloud, Twitch e da tutti gli altri siti supportati da yt-dlp.
- **Download segmentato nativo**, senza aria2 né altri programmi:
  - i file diretti vengono divisi in blocchi e scaricati con più connessioni HTTP in parallelo;
  - si riprende da dove ci si era fermati;
  - i flussi HLS/DASH (RaiPlay, Twitch…) scaricano i frammenti in parallelo tramite yt-dlp;
  - più download insieme, con un limite comune di connessioni.
- **Scelta precisa dei formati**: risoluzione, codec, più tracce audio insieme (per esempio italiano + audiodescrizione), sottotitoli originali o automatici, anche da soli.
- **MP4 o MKV**, audio in **MP3**, **M4A** o formato originale. Unione audio/video senza ricodifica.
- **Tema chiaro e scuro**, che segue Windows o si sceglie a mano.
- **Italiano e inglese**: la lingua segue quella di Windows o si sceglie dal menu ⋯ → Lingua / Language.
- **Area di notifica**: chiudendo la finestra continua a scaricare e avvisa a fine download.
- **Si aggiorna da sé**: scarica e aggiorna yt-dlp, FFmpeg e Deno dal menu.

## Installazione

Dalla pagina [Releases](https://github.com/Kyo89-sudo/Stackly/releases/latest):

- **`StacklySetup-x.y.z.exe`**: installa per il tuo utente in `%LOCALAPPDATA%\Programs\Stackly`. Non chiede permessi di amministratore, crea i collegamenti e si disinstalla da *Impostazioni → App installate*.
- **`Stackly-x.y.z-portable.zip`**: un solo `Stackly.exe` da mettere dove vuoi. Impostazioni e componenti restano nella sua cartella (se non è scrivibile, in `%LOCALAPPDATA%\Stackly`).

Requisiti: Windows 10 o 11 a 64 bit. Al primo avvio Stackly propone di scaricare i componenti che mancano.

## Uso

1. Incolla uno o più link nella barra in alto e premi **Invio** o **Scarica**.
2. Il **profilo** a sinistra decide cosa scaricare. Il pulsante riassume le scelte attive (es. `Video + audio · 1080p · MP4`) e apre il menu delle opzioni.
3. L'icona **Formati** dentro la barra analizza il link e mostra tutti i formati disponibili, per scegliere a mano video, tracce audio e sottotitoli.

### Tipi di download

| Tipo | Opzioni |
|---|---|
| **Video + audio** | qualità (Auto, 4K … 360p, *Chiedi ogni volta*), contenitore MP4/MKV, sottotitoli nel video |
| **Solo video** | qualità, contenitore MP4/MKV |
| **Solo audio** | formato MP3 / M4A / originale, qualità (migliore, 320, 192, 128 kbps) |
| **Solo sottotitoli** | formato SRT o originale, anche quelli generati automaticamente |
| **Scegli ogni volta** | apre la finestra Formati per ogni link |

Tranne che con *Scegli ogni volta* c'è anche **Playlist intera**: se il link è una playlist o un canale, scarica tutti i video.

Con **Chiedi ogni volta** Stackly mostra solo le risoluzioni realmente disponibili per quel video, con la dimensione stimata.

### Coda

- Ogni riga mostra miniatura, avanzamento, velocità e tempo residuo.
- Le icone sulla riga annullano, riprovano o aprono la cartella, e rimuovono il download dall'elenco.
- Il clic destro offre anche apri file, copia URL e mostra nella cartella.
- **Riprova** riprende i download segmentati da dove si erano fermati.

### Tasti

| Tasto | Azione |
|---|---|
| `Ctrl+Maiusc+V` | incolla dagli appunti e scarica subito |
| `Ctrl+O` | apre la cartella dei download |
| `Invio` (nella coda) | apre il file |
| `Canc` | rimuove i download selezionati |
| `Ctrl+A` / `Ctrl+C` | seleziona tutto / copia gli URL |

## Componenti

| Componente | A cosa serve |
|---|---|
| `yt-dlp.exe` | estrazione dei link (obbligatorio) |
| `ffmpeg.exe` + `ffprobe.exe` | unione audio/video, MP3, sottotitoli |
| `deno.exe` | runtime JavaScript che yt-dlp usa per YouTube |

Finiscono nella cartella `bin\` di Stackly. Si aggiornano dal menu **⋯**, e la finestra *Informazioni* mostra versione e percorso di ciascuno.

## Installazione silenziosa

```
StacklySetup-x.y.z.exe /S                  installazione silenziosa
StacklySetup-x.y.z.exe /S /D=C:\Percorso   installazione silenziosa in una cartella
uninstall.exe /uninstall /S                disinstallazione silenziosa
```

## Compilazione

Ogni build produce in `dist\` l'app, l'installer e lo zip portatile.

**Visual Studio 2019/2022**

```bat
build.bat
```

**llvm-mingw / MinGW-w64**, anche da Linux:

```sh
./make_release.sh /percorso/llvm-mingw/bin/x86_64-w64-mingw32-
```

**CMake**

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
```

L'icona si rigenera con `python3 tools/make_icon.py` (Pillow + NumPy). Le traduzioni sono in `tools/lang_en.json`: `python3 tools/i18n.py` rigenera `src/lang_table.inc` e segnala i testi senza traduzione. I test portabili sono in `tests/`, per esempio:

```sh
g++ -std=c++17 -fsanitize=address,undefined tests/json_test.cpp -o json_test && ./json_test
```

## Struttura

```
src/main.cpp      finestra principale, coda, area di notifica, menu
src/ui.h          tema, pulsanti disegnati dall'app, righe della coda
src/dialogs.h     finestre Formati, Risoluzione, Impostazioni, Informazioni
src/engine.h      processi yt-dlp/ffmpeg, motore nativo, strumenti, analisi
src/net.h         download segmentato WinHTTP, miniature (WIC)
src/json.h        parser JSON per l'output di yt-dlp -J
src/app.h         tipi, palette, impostazioni, utilità
src/lang.h        localizzazione: TR(), scelta della lingua
src/lang_table.inc  tabella italiano → inglese (generata da tools/i18n.py)
installer/        StacklySetup (installer e disinstallatore)
tools/            icona (make_icon.py) e traduzioni (i18n.py, lang_en.json)
tests/            test portabili
docs/             screenshot
```

## Storico delle versioni

Clicca su una versione per vedere cosa è cambiato. I dettagli tecnici sono nel [changelog](CHANGELOG.md).

<details>
<summary><b>0.6.1</b> — icona nitida, creazione file più veloce</summary>

- L'icona nell'area di notifica è nitida a qualsiasi scala dello schermo e non sembra più sfocata con la barra chiara.
- "Creazione file…" è più veloce: gli MP4 uniti non vengono più riscritti una seconda volta.
- Mentre il file viene creato, una barra animata e il tempo trascorso mostrano che il lavoro prosegue.

</details>

<details>
<summary><b>0.6.0</b> — inglese e licenza GPL</summary>

- Interfaccia anche in **inglese**: segue la lingua di Windows o si sceglie dal menu ⋯ → Lingua / Language (serve un riavvio, che Stackly propone da solo). Anche l'installer parla inglese su Windows non italiano.
- Stackly è ora software libero con licenza **GNU GPL v3.0**.
- Il testo del profilo in alto non viene più tagliato.

</details>

<details>
<summary><b>0.5.3</b> — correzioni e robustezza</summary>

- Con più download insieme, uno già finito poteva restare bloccato su "Creazione file…" finché non finiva un altro.
- Titoli e nomi con caratteri strani non vengono più mostrati con lettere sbagliate.
- "Scegli ogni volta" e "Chiedi ogni volta" su un link a playlist rispondono subito.
- "Solo sottotitoli" segnala un errore se non ci sono sottotitoli nelle lingue scelte.
- Annullare funziona anche nei rari casi in cui prima non aveva effetto.
- Un download interrotto senza supporto alla ripresa non viene più dato per completato.
- Niente chiusure improvvise se Windows esaurisce le risorse per i thread.
- Menu ⋯ → "Stackly su GitHub" e "Segnala un problema..."; link al progetto nella finestra Informazioni.

</details>

<details>
<summary><b>0.5.2</b> — dialoghi rifiniti</summary>

- Un'unica casella di spunta quadrata per tutta l'app (Impostazioni, finestra Formati, liste).
- Nelle Impostazioni campi e pulsanti rettangolari, con cornice sottile che diventa rubino quando sono attivi.
- Frecce dei campi numerici nello stile dell'app; testo e cursore centrati nei campi.
- Se all'avvio è già aperta una copia di Stackly da un'altra cartella (per esempio una versione vecchia rimasta nell'area di notifica), Stackly chiede se chiuderla e avviarsi al suo posto.

</details>

<details>
<summary><b>0.5.1</b> — tema scuro nei dialoghi</summary>

- Prime caselle di spunta e cornici dei campi disegnate nello stile dell'app, al posto di quelle di sistema che stonavano nel tema scuro.

</details>

<details>
<summary><b>0.5.0</b> — nuova interfaccia, icona e tipi di download</summary>

- **Interfaccia sul modello di Stacher**: profilo a sinistra, barra del link al centro, *Scarica* e icone a destra; coda a righe con icone per annullare, riprovare, aprire la cartella e rimuovere; barra di stato con la cartella di destinazione.
- **Nuova icona 3D "Rubino"** e **palette magenta → rubino**, con pulsanti ridisegnati e un tema chiaro più sobrio.
- **Tipi di download**: Video + audio, Solo video, Solo audio, Solo sottotitoli, Scegli ogni volta. Il menu del profilo mostra solo le opzioni del tipo scelto.
- **Finestra Informazioni** con lo stato di yt-dlp, FFmpeg e Deno, e i pulsanti per aggiornarli.
- **Download segmentato più affidabile**: connessioni davvero separate, ripresa con "Riprova" dopo un errore, velocità corretta alla ripresa, annullamento immediato, file parziali eliminati quando si rimuove un download.
- Accenti corretti nei menu; l'installer ha la versione nel nome.

</details>

<details>
<summary><b>0.3</b> — interfaccia più sobria</summary>

- Angoli quasi squadrati, bordi sottili, testi secondari più leggibili.
- Opzioni con etichette esplicite (Tipo / Qualità / Formato).
- Qualità **Auto** e **Chiedi ogni volta**, con le risoluzioni realmente disponibili e la dimensione stimata.

</details>

<details>
<summary><b>0.2</b> — formati, download segmentato, tema scuro</summary>

- Interfaccia con anteprime dei video, tema chiaro/scuro.
- **Finestra Formati**: video, più tracce audio insieme, sottotitoli anche da soli, incorporati o come file, conversione in SRT.
- **MP4 / MKV** selezionabile.
- **Download segmentato nativo**, senza aria2: più connessioni per file, ripresa, più download insieme.
- Area di notifica, istanza singola, installer.

</details>

## Licenza

Stackly è software libero, distribuito con licenza [GNU GPL v3.0](LICENSE).
yt-dlp, FFmpeg e Deno sono programmi separati, con le proprie licenze, scaricati a parte.

## Note

Rispetta i termini d'uso dei siti da cui scarichi e il diritto d'autore. I contenuti protetti da DRM non sono scaricabili.

