# Changelog tecnico

Versioni `x.y.z`: `x` cambi incompatibili (impostazioni, formato dei file), `y` funzioni nuove,
`z` solo correzioni. Le novità viste dall'utente sono nel README; qui ci sono file, funzioni e motivazioni.

Progetto: <https://github.com/Kyo89-sudo/Stackly>

## 0.6.2 — "Creazione file" piu' veloce

- **"Creazione file" piu' veloce**: tolto `-movflags +faststart` dall'unione/remux del motore nativo; faceva
  riscrivere a ffmpeg l'intero MP4 una seconda volta per spostare l'indice, utile solo per lo streaming web.
- **"Creazione file" non sembra piu' bloccato**: barra indeterminata animata (tratto in gradiente che scorre)
  e tempo trascorso accanto alla scritta (`Item::procSince`); il timer ridisegna le righe in questa fase.

## 0.6.1 — icona nitida

- **Icona nella tray sfocata** (soprattutto con la barra chiara): due cause.
  - `TrayAdd` caricava l'icona con `SM_CXSMICON`, cioe' quasi sempre 16 px, che Windows ingrandiva a
    20/24 px con lo schermo al 125–150 %. Ora `LoadSmallAppIcon` usa `LoadIconMetric(LIM_SMALL)`;
    `TrayRefreshIcon` la ricarica su `WM_DPICHANGED` e su `WM_SETTINGCHANGE` (metriche delle icone).
    L'iconcina della barra del titolo si aggiorna ai DPI della finestra (`GetSystemMetricsForDpi`).
  - Le misure fino a 32 px dell'icona avevano ombre sfumate (cornice, freccia, play) che su fondo chiaro
    diventano un alone. `tools/icon3d.py`: modalita' `CRISP` per le misure piccole, senza ombre, con i
    bordi della cornice allineati ai pixel (`snap`) e un leggero `UnsharpMask` dopo il ridimensionamento.

## 0.6.0 — localizzazione inglese e licenza GPL

### Localizzazione
- **`src/lang.h`**: `TR(const wchar_t*)` restituisce il testo italiano o la traduzione inglese da `kTrTable`
  (mappa costruita alla prima chiamata; senza traduzione resta l'italiano). `N_()` marca i testi delle
  tabelle costanti (`Choice`), tradotti al momento dell'uso: `Choice` ha ora `label()/value()/hint()`.
- **Tabella generata**: `tools/i18n.py` estrae `TR(L"...")`/`N_(L"...")` dai sorgenti e i testi di
  `app.rc` (titoli, etichette, pulsanti, voci di menu), li confronta con `tools/lang_en.json` e scrive
  `src/lang_table.inc` (letterali ASCII con `\uXXXX`). Esce con errore se manca una traduzione;
  `make_release.sh` lo esegue prima della build. 230 testi.
- **Risorse**: `TranslateWindow` (Impostazioni, Informazioni) e `TranslateMenu` (menu della coda)
  traducono a runtime i testi dei dialoghi e dei menu caricati da risorsa.
- **Lingua**: `SetLanguage(0 Windows | 1 it | 2 en)` letta dall'INI (`Language`) all'inizio di `wWinMain`,
  prima di qualunque messaggio. Menu ⋯ → *Lingua / Language*; il cambio chiede il riavvio (domanda
  bilingue) e avvia la nuova istanza dopo `QuitApp`.
- I testi composti sono stati spezzati in parti traducibili (contatori della barra di stato, riepiloghi
  della finestra Formati); le righe `ERROR:` mantengono il prefisso fuori dalla traduzione, perché
  `OnLine` lo riconosce.
- **Installer**: `T(it, en)` in base a `GetUserDefaultUILanguage()`.

### Altro
- Licenza **GPL-3.0** (`LICENSE`, testo ufficiale); citata in README e nella finestra Informazioni.
- Pulsante profilo: margine fisso portato a 92 px e massimo a 420 px (`Layout`): il testo veniva tagliato
  ("Video + audio · Auto · M…").
- README con screenshot (`docs/`) e sezione in inglese.

## 0.5.3 — correzioni e robustezza

### Processi figli (`engine.h`)
- **`Spawn`: ereditarietà degli handle ristretta** con `STARTUPINFOEXW` + `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`
  (solo stdout/stderr/stdin del figlio). Prima `bInheritHandles = TRUE` faceva ereditare a ogni figlio
  *tutti* gli handle ereditabili del processo, compresi i lati di scrittura dei tubi degli altri download
  avviati in parallelo: il `ReaderProc` di un download non riceveva EOF finché non terminava anche l'altro
  processo, e l'elemento restava su "Creazione file…". Se la lista viene rifiutata si riprova con l'avvio
  classico; `si.cb` è impostato in base alla struttura effettivamente usata.
- **Annullamento senza job object**: se `AssignProcessToJobObject` fallisce, `RunCaptureEx` passa a
  `onJob` l'handle del processo; `NativeCtx::Abort`/`SetJob` ripiegano su `TerminateProcess` quando
  `TerminateJobObject` fallisce. Prima l'annullamento durante l'estrazione o l'unione con ffmpeg poteva
  non avere effetto.
- **Messaggi di fine non perdibili**: nuovi `PostRetry`/`PostStrRetry` (`app.h`) ritentano `PostMessage`
  per qualche secondo se la coda è piena (limite di 10.000 messaggi). Usati per `WM_APP_DONE`,
  `WM_APP_FALLBACK`, `WM_APP_ANALYZED`, `WM_APP_TOOLDONE`: perderne uno lasciava l'elemento bloccato.

### Decodifica testo
- **`Utf8ToW`** (`app.h`): UTF-8 tollerante (sequenze non valide → U+FFFD) per le righe di yt-dlp in
  `ReaderProc`. Prima `BytesToW` usava `MB_ERR_INVALID_CHARS` e al primo byte non valido decodificava
  l'intera riga con la codepage ANSI, rovinando titoli e percorsi.
- **`json.h`**: surrogati UTF-16 isolati (`\ud83d` senza la metà bassa, o bassa da sola) diventano U+FFFD
  invece di produrre UTF-8 non valido; un'alta seguita da un `\u` non basso non perde più il carattere
  successivo. La scansione dei numeri non consuma più byte nulli (`strchr` trova anche il terminatore).
  Coperto da test (`tests/json_test.cpp`, compilati con ASan/UBSan): oggetti, emoji, surrogati, troncamenti,
  annidamento eccessivo, `NaN`/`Infinity`.

### Download
- **`net::Download` senza Range**: se la dimensione è nota e la connessione si chiude prima della fine,
  ora è un errore ("download incompleto") invece di un falso successo con file troncato.
- **Thread di lavoro**: `std::thread` può lanciare `std::system_error` se le risorse sono esaurite; prima
  l'eccezione non gestita terminava il programma. Ora `net::Download` prosegue con i thread già avviati
  (errore se nessuno), `NativeProc` ripiega su yt-dlp.
- **Analisi e motore nativo con `--flat-playlist`**: su un link a playlist `-J` estraeva tutte le voci
  prima di accorgersene (minuti su playlist lunghe); ora la risposta `_type: playlist` arriva subito.
  Nessun effetto sui link a un singolo video.

### Solo sottotitoli
- Con `--skip-download` yt-dlp non stampa `after_move`: il percorso del file si ricava ora dalla riga
  `Writing video subtitles to:` (`OnLine`, contatore `Item::subsWritten`). Se il processo termina con
  successo ma non ha scritto nessun sottotitolo, l'elemento va in errore con le lingue richieste invece
  di risultare "Completato" senza file. Dopo `--convert-subs srt` il percorso punta al `.srt`.
- `RetryItem` azzera anche `subsWritten` e `filePath`.

### Progetto
- Link al repository: voci "Stackly su GitHub" e "Segnala un problema..." nel menu ⋯ (`ID_HELP_GITHUB`,
  `ID_HELP_ISSUES`), link cliccabile nella finestra Informazioni (`AboutCtx::link`, hover con
  `TrackMouseEvent`, cursore a mano via `WM_SETCURSOR`/`DWLP_MSGRESULT`); l'URL è anche nel testo copiato.
  Costanti `kRepoUrl`, `kIssuesUrl`, `kRepoShort` in `app.h`.
- Nuova cartella `tests/` (test portabili, non servono Windows né il toolchain MinGW).

## 0.5.2

- **Casella di spunta unica** (`DrawCheckBox`, `ui.h`, 16 px, raggio 3 px, spunta tracciata con GDI+):
  usata da `BK_CHECK` e da `MakeCheckImages` (image list di stato delle ListView). Prima c'erano tre
  disegni diversi (glifo del font di icone, 18 px, raggio 5).
- **Impostazioni rettangolari**: proprietà finestra `stk.rect` letta in `SpecFor` → raggio 4 px invece
  della pillola; cornici dei campi raggio 3 px.
- **Bug della prima build 0.5.2**: i tre `IDC_SET_*` di spunta erano finiti nel `case` di `BK_SECONDARY`
  in `SpecFor` e venivano disegnati come pulsanti a pillola con testo centrato. Spostati nel gruppo `BK_CHECK`.
- **Frecce dei campi numerici**: `SpinSubclass` ridisegna gli UpDown (glifi `E70E`/`E70D`, hover in tinta).
- **Testo dei campi centrato**: senza bordo di sistema un EDIT allinea il testo in alto. `MarkFrame`
  riduce il controllo all'altezza della riga (`tmHeight + 2`) e lo centra; l'altezza originale è salvata
  in `stk.fh` e usata da `FrameRect`. Clic nella cornice fuori dal campo → `SetFocus` (`FrameMessages`).
- **Istanza vecchia aperta**: `HandleOtherInstance` (`main.cpp`) confronta il percorso dell'eseguibile
  dell'altra istanza (`QueryFullProcessImageNameW`); se diverso chiede se chiuderla (`ID_TRAY_EXIT`,
  attesa fino a 60 s) e avviare questa.

## 0.5.1

- Prime spunte disegnate dall'app (`OwnerDrawButton`, `UseAppCheckboxes`) e cornici arrotondate dei campi
  (`MarkFrame`, `PaintDialogFrames`, `FrameMessages`) in Impostazioni, Formati e Risoluzione.

## 0.5.0

Comprende anche la 0.4.0, build intermedia mai pubblicata a sé.

### Interfaccia
- **Layout sul modello di Stacher**: intestazione su una riga (profilo, barra del link con *Incolla* e
  *Formati* interni, Scarica, tema/impostazioni/menu), coda a righe con azione + rimuovi
  (`CardGeo::remove`, `g_hotPart`), barra di stato con la cartella (`BK_FOOT`). Rimossa la barra opzioni
  (`g_rcOpts`, `g_rcLabels`, pulsanti `IDC_B_KIND/QUALITY/FILEFMT/PLAYLIST/SUBS`).
- **Icona 3D "Rubino"** generata da `tools/icon3d.py` (`python3 tools/make_icon.py`, Pillow + NumPy),
  10 misure da 16 a 256 px.
- **Tipi di download** (`DlKind`): aggiunti `DK_VIDEOONLY = 3` e `DK_SUBS = 4`; i valori 0–2 restano quelli
  salvati nell'INI. `kKindOrder` dà l'ordine del menu; `kSubFmts` + `Preset::subfmt` (INI `SubFormat`).
  `FormatArgs`: solo video `-f "bv/bv*" ... --remux-video` con `VideoRemuxer:-an`; solo sottotitoli
  `--skip-download --write-subs [--write-auto-subs] [--convert-subs srt]`. `ItemWantsSubs` esclude
  `DK_SUBS` dal motore nativo. `PresetLabel` produce sempre "Tipo · dettagli"; `KindGlyph` per il profilo.
- **Menu profilo** ricostruito (`ProfileMenu`): tipo in alto come radio, poi solo le opzioni del tipo.
- **Finestra Informazioni** (`AboutProc`, `IDD_ABOUT`): disegnata in `AboutPaint`; versioni dei
  componenti lette in background (`AboutWork` → `WM_APP_ABOUTVER`).
- **Accenti nelle risorse**: llvm-windres ignora `#pragma code_page(65001)`; `make_release.sh` passa
  `--codepage=65001`.
- **Palette Rubino** (`Palette::gradA/gradB`, `FillRoundGrad`); tema chiaro con neutri grigi.

### Motore
- **Motore segmentato** (`net.h`, `engine.h`):
  - HTTP/2 disattivato per i blocchi (`OpenSession(ua, h2=false, maxConns)`): WinHTTP multiplexava tutte
    le richieste allo stesso host su una sola connessione TCP;
  - `Job::resumed`: i byte già presenti alla ripresa non entrano nel calcolo di velocità/ETA;
  - errore dopo ≥ 8 MB scaricati in sessione: i `.part`/`.stkp` restano e "Riprova" riprende con link
    nuovi, invece di cancellarli e ripartire con yt-dlp;
  - attese tra i tentativi interrompibili (`WaitOrCancel`), sonda (`Probe`) annullabile via `NativeCtx::seg`;
  - verifica di `Content-Range` sulle risposte 206;
  - `[SKX]` comunica i file parziali; `RemoveItem` li elimina per gli elementi non completati.

### Build
- Installer con versione nel nome (`StacklySetup-x.y.z.exe`) per evitare la cache icone di Explorer.
- `make_release.sh` compila anche da Linux con llvm-mingw (`./make_release.sh` con il toolchain nel PATH).

## 0.3

- Interfaccia più sobria: angoli quasi squadrati, bordi sottili, testi secondari più leggibili.
- Opzioni con etichette esplicite (Tipo / Qualità / Formato) al posto dei chip.
- Qualità **Auto** e **Chiedi ogni volta** (`QuickProc`, `BuildQuickRows`): risoluzioni realmente disponibili
  con dimensione stimata da `filesize`/`filesize_approx` o `tbr × durata`.

## 0.2

- Interfaccia a card con anteprime (miniature decodificate con WIC in `net::DecodeThumb`), tema chiaro/scuro
  (`SystemIsDark`, API non documentate di uxtheme per menu scuri), icone Segoe Fluent/MDL2.
- **Finestra Formati** (`PickerProc`): video, più tracce audio (`--audio-multistreams`), sottotitoli anche
  da soli, incorporati o come file, conversione in SRT.
- **MP4 / MKV** per preset e scelte manuali.
- **Download segmentato nativo** (`net.h`): blocchi da 4 MiB con HTTP Range su N connessioni WinHTTP,
  ripresa tramite mappa `.stkp`, file sparse; HLS/DASH lasciati a yt-dlp con `-N`; budget globale di
  24 connessioni (semaforo `g_connSlots`); ripiego automatico su yt-dlp.
- Unione senza ricodifica (`-c copy`), yt-dlp e ffmpeg a priorità sotto il normale dentro un job object.
- Area di notifica, istanza singola (mutex `Local\Stackly.SingleInstance`), installer `StacklySetup.exe`.
