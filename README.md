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

## Novità della 0.5.3

Correzioni e robustezza (dettagli tecnici in `CHANGELOG.md`):

- con più download insieme, uno già finito poteva restare bloccato su "Creazione file…" finché non finiva un altro;
- titoli e nomi con caratteri strani non vengono più mostrati con lettere sbagliate;
- "Scegli ogni volta" e "Chiedi ogni volta" su un link a playlist rispondono subito invece di analizzare tutta la playlist;
- "Solo sottotitoli" segnala un errore se il video non ha sottotitoli nelle lingue scelte, invece di dire "Completato" senza file;
- annullare funziona anche nei rari casi in cui prima non aveva effetto;
- un download interrotto senza supporto alla ripresa non viene più dato per completato con il file troncato;
- niente chiusure improvvise se Windows esaurisce le risorse per i thread;
- nel menu ⋯ "Stackly su GitHub" e "Segnala un problema..."; il link al progetto è anche nella finestra Informazioni.

## Novità della 0.5.2

- **Dialoghi rifiniti, anche nel tema scuro**: un'unica casella di spunta quadrata per tutta l'app (Impostazioni, finestra Formati, liste), con spunta bianca su gradiente; nelle Impostazioni campi e pulsanti rettangolari con cornice sottile (rubino quando attivi) al posto di quella incassata di sistema.

- **Copia vecchia ancora aperta**: se all'avvio c'è già un'altra copia di Stackly aperta da un percorso diverso (per esempio una versione precedente rimasta nell'area di notifica), Stackly chiede se chiuderla e avviarsi al suo posto, invece di riportare in primo piano quella vecchia.

## Novità della 0.5.1

- **Tema scuro rifinito nei dialoghi**: prime caselle di spunta e cornici dei campi nello stile dell'app.

## Novità della 0.5

- **Interfaccia ridisegnata sul modello di Stacher**, funzioni invariate:
  - intestazione su una riga: **profilo** a sinistra, barra del link al centro (con *Incolla* e *Formati* dentro il campo), **Scarica** e icone tema / impostazioni / menu a destra;
  - il **profilo** riassume le scelte attive (es. `Video · 1080p · MP4 · Playlist`) e apre un menu con Tipo, Qualità, Formato, Playlist, Sottotitoli e "Scegli i formati";
  - coda a **righe** con miniatura, stato, barra sottile e icone d'azione (annulla / riprova / apri cartella e **rimuovi**);
  - **barra di stato** in basso: cartella di destinazione (clic = apri o cambia) a sinistra, contatori a destra.
- **Nuova icona 3D "Rubino"**: lettore video in rilievo (cornice magenta → rubino, schermo prugna con play e barra di streaming) e freccia di download che esce dallo schermo. Si rigenera con `python3 tools/make_icon.py` (Pillow + NumPy).
- **Palette Rubino**: tema scuro con neutri tinti di prugna; tema chiaro con grigi neutri e card bianche, colore solo su pulsante principale, barre e icone attive. L'installer ora si chiama `StacklySetup-<versione>.exe`.
- **Pulsanti ridisegnati**: *Scarica* a pillola in gradiente magenta → rubino con riflesso; campi e profilo a pillola; icone con alone in tinta al passaggio del mouse; barre di avanzamento e spunte in gradiente; alone attorno alla barra del link quando è attiva.
- **Tipi di download coerenti**: *Video + audio*, *Solo video*, *Solo audio*, *Solo sottotitoli*, *Scegli ogni volta*. Il menu del profilo mostra solo le opzioni del tipo scelto (qualità e contenitore per il video, formato e bitrate per l'audio, formato SRT/originale e automatici per i sottotitoli), e l'icona del profilo cambia con il tipo.
- **Finestra Informazioni nuova**: icona grande, versione, stato di yt-dlp / FFmpeg / Deno con versione e percorso, pulsanti per aggiornare o scaricare i componenti, aprire la cartella strumenti e copiare le informazioni.
- **Accenti corretti** nei menu e nelle finestre compilate con MinGW/LLVM (`windres --codepage=65001`).
- **Download segmentato più coerente**:
  - le connessioni multiple sono davvero connessioni TCP separate (HTTP/2 disattivato per i blocchi: prima WinHTTP poteva multiplexarle tutte su una sola);
  - se un download si interrompe dopo aver scaricato qualcosa, i blocchi completati restano e **Riprova** riprende da lì con link nuovi, invece di ricominciare da zero con yt-dlp;
  - velocità e tempo residuo non sballano più alla ripresa (i byte già su disco non contano come velocità);
  - l'annullamento è immediato anche durante le attese tra un tentativo e l'altro e durante la verifica iniziale del server;
  - si controlla che il server restituisca esattamente il blocco richiesto;
  - rimuovendo dall'elenco un download non finito si eliminano anche i suoi file parziali (`.part`, `.stkp`).

## Novità della 0.3

- Interfaccia più sobria: angoli quasi squadrati, bordi sottili, testi secondari più leggibili.
- Opzioni con etichette esplicite (Tipo / Qualità / Formato) al posto dei chip.
- Qualità **Auto** e **Chiedi ogni volta**: finestra con le risoluzioni realmente disponibili e la dimensione stimata.

## Novità della 0.2

- **Interfaccia stile Stacher**, tema **chiaro/scuro** (segue Windows oppure fisso), accento rosso,
  icone Fluent/MDL2 di sistema, coda a card con **anteprime** dei video e barra di avanzamento.
- **Finestra Formati**: analizza il link e mostra tutti i formati disponibili.
  - **Video**: risoluzione, codec, bitrate, dimensione, tipo (diretto/HLS/DASH).
  - **Audio**: anche più tracce insieme, ad esempio italiano + audiodescrizione su RaiPlay.
  - **Sottotitoli**: originali e automatici, scaricabili **anche da soli**.
  - Si sceglie se incorporarli o tenerli come file, ed eventualmente convertirli in SRT.
- **MP4 / MKV** selezionabile sia per i preset sia nella finestra Formati.
- **Download segmentato nativo**, senza aria2 né altri tool:
  - **file singoli** (MP4 diretti, flussi YouTube): Stackly li divide in blocchi da 4 MB e li scarica con N connessioni HTTP Range (WinHTTP). Riprende da dove era se interrotto, e usa un file sparse per evitare blocchi del disco su file grandi;
  - **HLS/DASH** (RaiPlay, Mediaset, Twitch…): frammenti in parallelo con il downloader interno di yt-dlp (`-N`);
  - **più download insieme** dalla coda;
  - **budget globale** di 24 connessioni condiviso tra tutti i download.
  Se il motore nativo non può gestire un link (cookie, protocolli particolari, errori), passa da solo a yt-dlp.
- **Creazione del file veloce**: unione audio/video senza ricodifica (`-c copy`), preferenza per codec compatibili con MP4, yt-dlp e ffmpeg a priorità sotto il normale. L'interfaccia non si blocca mai.
- **Area di notifica**: chiudendo la finestra continua in background (disattivabile), notifica a fine download.
- **Istanza singola**: riaprendo Stackly torna in primo piano quella già aperta.
- **Installer nostro**: `StacklySetup.exe`.

## Uso

La barra opzioni ha campi espliciti:

| Campo | Valori |
|---|---|
| **Tipo** | Video · Solo audio · Scegli ogni volta (finestra Formati completa) |
| **Qualità** | video: Auto, 2160p (4K), 1440p, 1080p (Full HD), 720p (HD), 480p, 360p, **Chiedi ogni volta**; audio: Migliore (VBR), 320, 192, 128 kbps |
| **Formato** | video: MP4 (compatibile con tutto) o MKV (qualsiasi codec, più tracce); audio: MP3, M4A, Originale |
| **Playlist / Sottotitoli** | caselle di spunta |
| **Salva in** | cartella di destinazione |

- Incolla uno o più link e premi **Invio** (o **Scarica**).
- Con **Qualità = Chiedi ogni volta** Stackly analizza il link e mostra solo le risoluzioni disponibili per quel video, con la dimensione stimata e l'opzione "Solo audio". **Avanzate…** apre la scelta completa.
- **Formati**: scelta completa di video, tracce audio e sottotitoli (anche solo sottotitoli).
- **Ctrl+Maiusc+V**: incolla dagli appunti e scarica subito.
- Il pulsante su ogni riga annulla, riprova o apre la cartella. Il clic destro offre apri file, mostra nella cartella, copia URL, riprova, annulla, rimuovi.
- Nella lista: `Canc` rimuove, `Ctrl+A` seleziona tutto, `Ctrl+C` copia gli URL, `Invio` apre il file.

## Componenti

Al primo avvio Stackly propone di scaricare in `bin\` quello che manca, usando il proprio motore segmentato:

| Componente | A cosa serve |
|---|---|
| `yt-dlp.exe` | estrazione dei link (obbligatorio) |
| `ffmpeg.exe` + `ffprobe.exe` | unione audio/video, MP3, sottotitoli incorporati |
| `deno.exe` | runtime JavaScript che yt-dlp usa per YouTube |

Si aggiornano dal menu **⋯**.

## Installer

`StacklySetup.exe` installa per l'utente corrente in `%LOCALAPPDATA%\Programs\Stackly`, quindi **non chiede permessi di amministratore**.

- Crea i collegamenti in Start e sul desktop.
- Registra Stackly in *Impostazioni > App installate*, con il proprio disinstallatore.
- La disinstallazione chiede se eliminare anche impostazioni e componenti scaricati.

Opzioni da riga di comando:

```
StacklySetup.exe /S                 installazione silenziosa
StacklySetup.exe /S /D=C:\Percorso  installazione silenziosa in una cartella
uninstall.exe /uninstall /S         disinstallazione silenziosa
```

## Compilazione

Ogni build produce in `dist\` l'app, l'installer e lo zip portable.

**Visual Studio 2019/2022**:

```bat
build.bat
```

**llvm-mingw / MinGW-w64** (anche da Linux):

```sh
./make_release.sh /percorso/llvm-mingw/bin/x86_64-w64-mingw32-
```

**CMake**:

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
```

## Struttura

```
src/main.cpp      finestra principale, coda, tray, menu
src/ui.h          tema, pulsanti owner-drawn, card della coda
src/dialogs.h     finestra Formati e Impostazioni
src/engine.h      processi yt-dlp/ffmpeg, percorso nativo, strumenti, analisi
src/net.h         motore segmentato WinHTTP, miniature (WIC)
src/json.h        parser JSON per l'output di yt-dlp -J
src/app.h         tipi, impostazioni, utilità
installer/        StacklySetup (installer + disinstallatore)
```

## Note

- Requisiti: Windows 10 o 11, x64.
- Rispetta i termini d'uso dei siti da cui scarichi e il diritto d'autore. I contenuti protetti da DRM non sono scaricabili.
