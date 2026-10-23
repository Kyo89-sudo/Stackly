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

L'icona si rigenera con `python3 tools/make_icon.py` (Pillow + NumPy). I test portabili sono in `tests/`, per esempio:

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
installer/        StacklySetup (installer e disinstallatore)
tools/            generatore dell'icona
tests/            test portabili
```

## Novità della 0.5.3

Correzioni e robustezza; i dettagli per tutte le versioni sono nel [changelog](CHANGELOG.md).

- Con più download insieme, uno già finito poteva restare bloccato su "Creazione file…" finché non finiva un altro.
- Titoli e nomi con caratteri strani non vengono più mostrati con lettere sbagliate.
- "Scegli ogni volta" e "Chiedi ogni volta" su un link a playlist rispondono subito.
- "Solo sottotitoli" segnala un errore se non ci sono sottotitoli nelle lingue scelte.
- Annullare funziona anche nei rari casi in cui prima non aveva effetto.
- Un download interrotto senza supporto alla ripresa non viene più dato per completato.
- Niente chiusure improvvise se Windows esaurisce le risorse per i thread.
- Menu ⋯ → "Stackly su GitHub" e "Segnala un problema..."; link al progetto nella finestra Informazioni.

## Note

Rispetta i termini d'uso dei siti da cui scarichi e il diritto d'autore. I contenuti protetti da DRM non sono scaricabili.
