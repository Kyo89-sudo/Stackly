// Stackly - interfaccia per yt-dlp ispirata a Stacher (Win32 C++17, un solo .exe)
// Coda con anteprime, formati manuali (video/audio/sottotitoli), download segmentato nativo,
// tema chiaro/scuro, area di notifica.
#include "dialogs.h"

// ---------------------------------------------------------------------------
// Lista (ListBox owner-drawn con card)
// ---------------------------------------------------------------------------
static int ListIndex(int id) {
    int n = (int)SendMessageW(g_list, LB_GETCOUNT, 0, 0);
    for (int i = 0; i < n; ++i)
        if ((int)SendMessageW(g_list, LB_GETITEMDATA, i, 0) == id) return i;
    return -1;
}

static void InvalidateItem(int id) {
    if (Item* it = FindItem(id)) it->dirty = false;
    int i = ListIndex(id);
    if (i < 0) return;
    RECT r;
    if (SendMessageW(g_list, LB_GETITEMRECT, i, (LPARAM)&r) != LB_ERR) InvalidateRect(g_list, &r, FALSE);
}

static void UpdateStatus() {
    InvalidateRect(g_hwnd, &g_rcFooter, FALSE);
}

static void RefreshList() {
    bool empty = g_items.empty();
    if ((IsWindowVisible(g_list) != FALSE) == empty) ShowWindow(g_list, empty ? SW_HIDE : SW_SHOW);
    InvalidateRect(g_hwnd, &g_rcList, FALSE);
    UpdateStatus();
}

static std::vector<int> SelectedIds() {
    std::vector<int> ids;
    int n = (int)SendMessageW(g_list, LB_GETSELCOUNT, 0, 0);
    if (n <= 0) return ids;
    std::vector<int> idx(n);
    n = (int)SendMessageW(g_list, LB_GETSELITEMS, n, (LPARAM)idx.data());
    for (int i = 0; i < n; ++i) ids.push_back((int)SendMessageW(g_list, LB_GETITEMDATA, idx[i], 0));
    return ids;
}

// ---------------------------------------------------------------------------
// Aggiunta link
// ---------------------------------------------------------------------------
static std::vector<std::wstring> ExtractUrls(const std::wstring& text) {
    std::vector<std::wstring> urls;
    std::wstring tok;
    auto flush = [&] {
        while (!tok.empty() && wcschr(L"\"'<>()[],;", tok.back())) tok.pop_back();
        while (!tok.empty() && wcschr(L"\"'<>()[]", tok.front())) tok.erase(0, 1);
        if (StartsWith(tok, L"www.") || StartsWith(tok, L"youtu.be/") || StartsWith(tok, L"youtube.com/"))
            tok = L"https://" + tok;   // link incollati senza schema
        if (tok.find(L"://") != std::wstring::npos && std::find(urls.begin(), urls.end(), tok) == urls.end())
            urls.push_back(tok);
        tok.clear();
    };
    for (wchar_t ch : text) {
        if (iswspace(ch)) flush(); else tok += ch;
    }
    flush();
    // Nessun URL: accetta le ricerche di yt-dlp (es. "ytsearch:titolo canzone")
    std::wstring t = Trim(text);
    if (urls.empty() && (StartsWith(t, L"ytsearch") || StartsWith(t, L"scsearch"))) urls.push_back(t);
    return urls;
}

static bool EnsureFolder() {
    if (g_set.folder.empty()) g_set.folder = DefaultDownloads();
    if (!DirExists(g_set.folder)) {
        int r = SHCreateDirectoryExW(g_hwnd, g_set.folder.c_str(), nullptr);
        if (r != ERROR_SUCCESS && r != ERROR_ALREADY_EXISTS && r != ERROR_FILE_EXISTS) {
            MessageBoxW(g_hwnd, (L"Impossibile usare la cartella di destinazione:\n" + g_set.folder).c_str(), kAppName,
                        MB_ICONERROR);
            return false;
        }
    }
    return true;
}

static void QueueUrl(const std::wstring& u, const PickResult* pick, const Preset* override = nullptr) {
    auto it = std::make_unique<Item>();
    it->id = g_nextId++;
    it->url = u;
    it->folder = g_set.folder;
    it->preset = override ? *override : g_set.preset;
    if (it->preset.kind == DK_MANUAL) it->preset.kind = DK_VIDEO;
    if (it->preset.vq == kAskQuality && it->preset.height <= 0) it->preset.vq = 0;   // senza scelta: Auto
    it->container = it->preset.container;
    it->playlist = g_set.playlist;
    it->subs = g_set.subs && it->preset.kind == DK_VIDEO;
    if (pick) {
        it->custom = true;
        it->customArgs = pick->args;
        it->customLabel = pick->label;
        it->customKind = pick->kind;
        it->customSubs = pick->subs;
        it->container = pick->container;
        it->playlist = false;
    }
    SendMessageW(g_list, LB_ADDSTRING, 0, (LPARAM)it->id);
    g_items.push_back(std::move(it));
}

static void ScrollToEnd() {
    int n = (int)SendMessageW(g_list, LB_GETCOUNT, 0, 0);
    if (n > 0) SendMessageW(g_list, LB_SETTOPINDEX, n - 1, 0);
}

static void AddUrls(const std::wstring& text, bool forceManual = false) {
    std::vector<std::wstring> urls = ExtractUrls(text);
    if (urls.empty()) {
        if (!Trim(text).empty())
            MessageBoxW(g_hwnd, L"Non ho trovato link validi (devono iniziare con http:// o https://).", kAppName,
                        MB_ICONINFORMATION);
        SetFocus(g_url);
        return;
    }
    if (!EnsureFolder()) return;
    SaveSettings();
    const bool ask = KindIsVideo(g_set.preset.kind) && g_set.preset.vq == kAskQuality && !g_set.playlist;
    if (forceManual || g_set.preset.kind == DK_MANUAL || ask) {
        for (auto& u : urls) g_analyzeQueue.push_back({ u, ask && !forceManual });
        StartNextAnalysis();
    } else {
        for (auto& u : urls) QueueUrl(u, nullptr);
        ScrollToEnd();
        RefreshList();
        PumpQueue();
    }
    SetWindowTextW(g_url, L"");
}

static void QueueAndStart(const std::wstring& url, const PickResult* pick, const Preset* pr) {
    if (!EnsureFolder()) return;
    QueueUrl(url, pick, pr);
    ScrollToEnd();
    RefreshList();
    PumpQueue();
}

static void OpenFullPicker(const JVal& root, const std::wstring& url) {
    PickerCtx ctx;
    ctx.url = url;
    ctx.hasFfmpeg = !ResolveTool(g_set.ffmpeg, L"ffmpeg.exe").empty();
    BuildPickerRows(root, ctx);
    if (ctx.vids.empty() && ctx.auds.empty() && ctx.subs.empty()) {
        MessageBoxW(g_hwnd, L"Nessun formato scaricabile trovato per questo link.", kAppName, MB_ICONINFORMATION);
        return;
    }
    if (DialogBoxParamW(g_inst, MAKEINTRESOURCEW(IDD_PICKER), g_hwnd, PickerProc, (LPARAM)&ctx) == IDOK)
        QueueAndStart(url, &ctx.out, nullptr);
}

static void OnAnalyzed(AnalyzeResult* raw) {
    std::unique_ptr<AnalyzeResult> r(raw);
    g_analyzing = false;
    g_footRight.clear();
    EnableWindow(g_bFormats, TRUE);
    UpdateStatus();
    if (!IsWindowVisible(g_hwnd)) ShowWindow(g_hwnd, SW_SHOW);
    JVal root;
    if (!r->err.empty()) {
        if (r->quick) {
            // l'analisi non e' riuscita: si scarica comunque in Auto e l'eventuale errore compare sulla card
            Preset pr = g_set.preset;
            pr.vq = 0;
            QueueAndStart(r->url, nullptr, &pr);
        } else {
            MessageBoxW(g_hwnd, (L"Non riesco ad analizzare il link:\n" + r->url + L"\n\n" + r->err).c_str(), kAppName,
                        MB_ICONWARNING);
        }
    } else if (!JParser(r->json.data(), r->json.size()).parse(root) || root.t != JVal::Obj) {
        MessageBoxW(g_hwnd, L"Risposta di yt-dlp non valida.", kAppName, MB_ICONWARNING);
    } else if (root.str("_type") == "playlist") {
        if (r->quick) {
            Preset pr = g_set.preset;
            pr.vq = 0;
            QueueAndStart(r->url, nullptr, &pr);
        } else {
            MessageBoxW(g_hwnd, L"Questo link è una playlist: per le playlist scegli il tipo Video o Solo audio "
                                L"e attiva \"Playlist\".", kAppName, MB_ICONINFORMATION);
        }
    } else if (r->quick) {
        QuickCtx q;
        q.container = g_set.preset.container;
        BuildQuickRows(root, q);
        if (q.rows.empty()) {
            OpenFullPicker(root, r->url);
        } else {
            INT_PTR res = DialogBoxParamW(g_inst, MAKEINTRESOURCEW(IDD_PICKER), g_hwnd, QuickProc, (LPARAM)&q);
            if (res == IDOK) {
                Preset pr = g_set.preset;
                pr.container = q.container;
                if (q.audioOnly) pr.kind = DK_AUDIO;
                else pr.height = q.height;
                QueueAndStart(r->url, nullptr, &pr);
            } else if (res == IDC_PK_ADV) {
                OpenFullPicker(root, r->url);
            }
        }
    } else {
        OpenFullPicker(root, r->url);
    }
    StartNextAnalysis();
}

static std::wstring ClipboardText() {
    std::wstring s;
    if (!OpenClipboard(g_hwnd)) return s;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (auto* p = static_cast<const wchar_t*>(GlobalLock(h))) { s = p; GlobalUnlock(h); }
    }
    CloseClipboard();
    return s;
}

static void SetClipboardText(const std::wstring& s) {
    if (!OpenClipboard(g_hwnd)) return;
    EmptyClipboard();
    size_t bytes = (s.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* p = GlobalLock(h)) {
            memcpy(p, s.c_str(), bytes);
            GlobalUnlock(h);
            if (!SetClipboardData(CF_UNICODETEXT, h)) GlobalFree(h);
        } else {
            GlobalFree(h);
        }
    }
    CloseClipboard();
}

static bool PickFolder(HWND owner, std::wstring& inout) {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return false;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_PICKFOLDERS);
    if (!inout.empty()) {
        IShellItem* si = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(inout.c_str(), nullptr, IID_PPV_ARGS(&si)))) {
            dlg->SetFolder(si);
            si->Release();
        }
    }
    bool ok = false;
    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItem* res = nullptr;
        if (SUCCEEDED(dlg->GetResult(&res))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(res->GetDisplayName(SIGDN_FILESYSPATH, &p))) { inout = p; CoTaskMemFree(p); ok = true; }
            res->Release();
        }
    }
    dlg->Release();
    return ok;
}

static void ShowInExplorer(const std::wstring& file, const std::wstring& fallbackDir) {
    if (FileExists(file)) {
        if (PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(file.c_str())) {
            SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
            ILFree(pidl);
            return;
        }
    }
    ShellExecuteW(g_hwnd, L"open", fallbackDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// ---------------------------------------------------------------------------
// Azioni sugli elementi
// ---------------------------------------------------------------------------
static void RetryItem(Item& it) {
    if (!(it.st == St::Error || it.st == St::Cancelled || it.st == St::Done)) return;
    it.st = St::Queued;
    it.err.clear(); it.pct.clear(); it.size.clear(); it.speed.clear(); it.eta.clear();
    it.pctValue = 0;
    it.plIdx = it.plCount = 0;
    it.subsWritten = 0;
    it.filePath.clear();
    it.nativeTried = false;
    it.usingNative = false;
    InvalidateItem(it.id);
}

static void RemoveItem(int id) {
    Item* it = FindItem(id);
    if (!it || IsActive(*it)) return;
    // Rimuovere un download non completato scarta anche i blocchi parziali del motore nativo
    if (it->st != St::Done)
        for (auto& p : it->parts) { DeleteFileW(p.c_str()); DeleteFileW((p + L".stkp").c_str()); }
    int i = ListIndex(id);
    if (i >= 0) SendMessageW(g_list, LB_DELETESTRING, i, 0);
    g_hotItem = -1;
    g_items.erase(std::remove_if(g_items.begin(), g_items.end(), [id](const std::unique_ptr<Item>& p) { return p->id == id; }),
                  g_items.end());
}

static void ItemAction(Item& it) {
    switch (it.st) {
    case St::Queued: case St::Starting: case St::Downloading: case St::Processing:
        CancelItem(it);
        PumpQueue();
        break;
    case St::Error: case St::Cancelled:
        RetryItem(it);
        PumpQueue();
        break;
    case St::Done:
        ShowInExplorer(it.filePath, it.folder);
        break;
    }
}

static void OnListCommand(int cmd) {
    std::vector<int> ids = SelectedIds();
    switch (cmd) {
    case ID_DL_CANCEL:
        for (int id : ids) if (Item* it = FindItem(id)) CancelItem(*it);
        PumpQueue();
        break;
    case ID_DL_RETRY:
        for (int id : ids) if (Item* it = FindItem(id)) RetryItem(*it);
        PumpQueue();
        break;
    case ID_DL_REMOVE:
        for (int id : ids) RemoveItem(id);
        RefreshList();
        break;
    case ID_DL_OPENFILE:
        if (!ids.empty()) if (Item* it = FindItem(ids[0])) {
            if (FileExists(it->filePath)) ShellExecuteW(g_hwnd, L"open", it->filePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            else ShowInExplorer(L"", it->folder);
        }
        break;
    case ID_DL_OPENITEMFOLDER:
        if (!ids.empty()) if (Item* it = FindItem(ids[0])) ShowInExplorer(it->filePath, it->folder);
        break;
    case ID_DL_COPYURL: {
        std::wstring s;
        for (int id : ids) if (Item* it = FindItem(id)) { if (!s.empty()) s += L"\r\n"; s += it->url; }
        if (!s.empty()) SetClipboardText(s);
        break;
    }
    }
}

static void ClearDone() {
    std::vector<int> ids;
    for (auto& p : g_items) if (p->st == St::Done) ids.push_back(p->id);
    for (int id : ids) RemoveItem(id);
    RefreshList();
}

// ---------------------------------------------------------------------------
// Sottoclasse della lista: hover, pulsante azione, tastiera
// ---------------------------------------------------------------------------
static void InvalidateIndex(int i) {
    RECT r;
    if (i >= 0 && SendMessageW(g_list, LB_GETITEMRECT, i, (LPARAM)&r) != LB_ERR) InvalidateRect(g_list, &r, FALSE);
}

static LRESULT CALLBACK ListSubclass(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    switch (msg) {
    case WM_MOUSEMOVE: {
        POINT pt{ (short)LOWORD(lp), (short)HIWORD(lp) };
        LRESULT r = SendMessageW(h, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y));
        int idx = HIWORD(r) ? -1 : (int)LOWORD(r);
        int part = 0;
        if (idx >= 0) {
            RECT ir;
            SendMessageW(h, LB_GETITEMRECT, idx, (LPARAM)&ir);
            CardGeo g = Geo(ir);
            Item* it = FindItem((int)SendMessageW(h, LB_GETITEMDATA, idx, 0));
            if (PtInRect(&g.action, pt)) part = 1;
            else if (PtInRect(&g.remove, pt) && it && !IsActive(*it)) part = 2;
            if (!PtInRect(&g.card, pt)) idx = -1;
        }
        if (idx != g_hotItem || part != g_hotPart) {
            int old = g_hotItem;
            g_hotItem = idx;
            g_hotPart = part;
            InvalidateIndex(old);
            InvalidateIndex(idx);
            TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, h, 0 };
            TrackMouseEvent(&t);
        }
        break;
    }
    case WM_MOUSELEAVE:
        if (g_hotItem >= 0) { int old = g_hotItem; g_hotItem = -1; g_hotPart = 0; InvalidateIndex(old); }
        break;
    case WM_SETCURSOR:
        if (g_hotPart) { SetCursor(LoadCursorW(nullptr, IDC_HAND)); return TRUE; }
        break;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        POINT pt{ (short)LOWORD(lp), (short)HIWORD(lp) };
        LRESULT r = SendMessageW(h, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y));
        if (!HIWORD(r)) {
            int idx = (int)LOWORD(r);
            RECT ir;
            SendMessageW(h, LB_GETITEMRECT, idx, (LPARAM)&ir);
            CardGeo g = Geo(ir);
            int id = (int)SendMessageW(h, LB_GETITEMDATA, idx, 0);
            Item* it = FindItem(id);
            if (PtInRect(&g.action, pt)) {
                if (msg == WM_LBUTTONDOWN && it) ItemAction(*it);
                return 0;   // il clic sul pulsante non cambia la selezione
            }
            if (PtInRect(&g.remove, pt) && it && !IsActive(*it)) {
                if (msg == WM_LBUTTONDOWN) { RemoveItem(id); g_hotPart = 0; RefreshList(); }
                return 0;
            }
        }
        break;
    }
    case WM_KEYDOWN: {
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if (wp == VK_DELETE) { OnListCommand(ID_DL_REMOVE); return 0; }
        if (ctrl && wp == 'A') { SendMessageW(h, LB_SETSEL, TRUE, -1); return 0; }
        if (ctrl && wp == 'C') { OnListCommand(ID_DL_COPYURL); return 0; }
        break;
    }
    case WM_ERASEBKGND: {
        // Riempie solo l'area sotto l'ultima card (le card sono disegnate in doppio buffer)
        HDC dc = (HDC)wp;
        RECT rc;
        GetClientRect(h, &rc);
        int n = (int)SendMessageW(h, LB_GETCOUNT, 0, 0);
        if (n > 0) {
            RECT last;
            if (SendMessageW(h, LB_GETITEMRECT, n - 1, (LPARAM)&last) != LB_ERR) rc.top = std::max(rc.top, last.bottom);
        }
        if (rc.bottom > rc.top) FillRect(dc, &rc, g_brBg);
        return 1;
    }
    }
    return DefSubclassProc(h, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Area di notifica (tray)
// ---------------------------------------------------------------------------
static void TrayAdd() {
    if (g_trayAdded) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.hIcon = (HICON)LoadImageW(g_inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                  GetSystemMetrics(SM_CYSMICON), 0);
    wcscpy_s(nid.szTip, L"Stackly");
    g_trayAdded = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    nid.uVersion = NOTIFYICON_VERSION_4;
    if (g_trayAdded) Shell_NotifyIconW(NIM_SETVERSION, &nid);
}

static void TrayRemove() {
    if (!g_trayAdded) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    g_trayAdded = false;
}

static void TrayTip(const std::wstring& tip) {
    if (!g_trayAdded) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_TIP | NIF_SHOWTIP;
    wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

static void ShowTrayBalloon(const std::wstring& title, const std::wstring& text) {
    if (!g_trayAdded) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON;
    wcsncpy_s(nid.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(nid.szInfo, text.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

static void ShowMain() {
    if (!IsWindowVisible(g_hwnd)) ShowWindow(g_hwnd, SW_SHOW);
    if (IsIconic(g_hwnd)) ShowWindow(g_hwnd, SW_RESTORE);
    SetForegroundWindow(g_hwnd);
}

static void TrayMenu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_TRAY_SHOW, L"Mostra Stackly");
    AppendMenuW(m, MF_STRING, ID_FILE_PASTE, L"Scarica il link negli appunti");
    AppendMenuW(m, MF_STRING, ID_FILE_OPENFOLDER, L"Apri cartella download");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_TRAY_EXIT, L"Esci");
    SetMenuDefaultItem(m, ID_TRAY_SHOW, FALSE);
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);
    TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, g_hwnd, nullptr);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    DestroyMenu(m);
}

static void UpdateTrayTip() {
    int a = 0;
    double sum = 0;
    for (auto& p : g_items) if (IsActive(*p)) { ++a; sum += p->pctValue; }
    if (a) {
        wchar_t b[96];
        swprintf(b, 96, L"Stackly – %d in download (%.0f%%)", a, sum / a);
        TrayTip(b);
    } else {
        TrayTip(L"Stackly");
    }
}

// ---------------------------------------------------------------------------
// Layout e disegno della finestra
// ---------------------------------------------------------------------------
static const int kHeaderH = 64, kFooterH = 34;

// Riepilogo del profilo attivo (pulsante in alto a sinistra, come il selettore di configurazione di Stacher)
static std::wstring ProfileText() {
    const Preset& p = g_set.preset;
    std::wstring t = PresetLabel(p, p.container, g_set.subs && p.kind == DK_VIDEO);
    if (g_set.playlist && p.kind != DK_MANUAL) t += L" \u00b7 Playlist";
    return t;
}

static void Layout() {
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    const int W = rc.right, H = rc.bottom;
    const int m = S(16);
    g_rcHeader = { 0, 0, W, S(kHeaderH) };
    g_rcFooter = { 0, H - S(kFooterH), W, H };
    g_rcList = { 0, g_rcHeader.bottom + 1, W, std::max<LONG>(g_rcHeader.bottom + 1, g_rcFooter.top) };

    HDWP d = BeginDeferWindowPos(12);
    auto mv = [&](HWND w, int x, int y, int ww, int hh) {
        if (d) d = DeferWindowPos(d, w, nullptr, x, y, std::max(ww, 0), std::max(hh, 0), SWP_NOZORDER | SWP_NOACTIVATE);
    };
    // intestazione su una riga: [icona][profilo] [barra link] [Scarica] [tema][impostazioni][menu]
    int hy = (g_rcHeader.top + g_rcHeader.bottom) / 2;
    const int gb = S(36), bh = S(38);
    int x = W - m;
    mv(g_bMore, x - gb, hy - gb / 2, gb, gb);       x -= gb + S(2);
    mv(g_bSettings, x - gb, hy - gb / 2, gb, gb);   x -= gb + S(2);
    mv(g_bTheme, x - gb, hy - gb / 2, gb, gb);      x -= gb + S(12);
    int wd = TextWidth(g_fBold, L"Scarica") + S(68);
    mv(g_bDownload, x - wd, hy - bh / 2, wd, bh);   x -= wd + S(10);

    int px = m + S(28) + S(12);
    int wp = std::clamp(TextWidth(g_fUi, ProfileText()) + S(70), S(180), S(320));
    mv(g_bProfile, px, hy - bh / 2, wp, bh);
    int ux = px + wp + S(10);
    g_rcUrl = { ux, hy - bh / 2, std::max(ux + S(160), x), hy + bh / 2 };
    int eh = S(22);
    mv(g_url, g_rcUrl.left + S(42), hy - eh / 2, g_rcUrl.right - S(90) - (g_rcUrl.left + S(42)), eh);
    mv(g_bPaste, g_rcUrl.right - S(80), hy - S(15), S(32), S(30));
    mv(g_bFormats, g_rcUrl.right - S(44), hy - S(15), S(32), S(30));

    // barra di stato: cartella di destinazione a sinistra
    int fy = (g_rcFooter.top + 1 + g_rcFooter.bottom) / 2, fh = S(26);
    int wf = std::min(TextWidth(g_fSmall, g_set.folder) + S(44), std::max(S(160), W / 2));
    mv(g_bFolder, m - S(8), fy - fh / 2, wf, fh);

    mv(g_list, 0, g_rcList.top + S(8), W, g_rcList.bottom - g_rcList.top - S(8));
    if (d) EndDeferWindowPos(d);
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

static void Paint(HDC hdc) {
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    MemDC m(hdc, rc.right, rc.bottom);
    FillRect(m.dc, &rc, g_brBg);
    FillRect(m.dc, &g_rcHeader, g_brHeader);
    FillRect(m.dc, &g_rcFooter, g_brHeader);
    {
        HBRUSH b = CreateSolidBrush(g_dark ? Mix(P.header, P.border, 0.6) : Mix(P.border, P.header, 0.35));
        RECT line{ 0, g_rcHeader.bottom, rc.right, g_rcHeader.bottom + 1 };
        FillRect(m.dc, &line, b);
        RECT fl{ 0, g_rcFooter.top, rc.right, g_rcFooter.top + 1 };
        FillRect(m.dc, &fl, b);
        DeleteObject(b);
    }
    const int mg = S(16);
    int hy = (g_rcHeader.top + g_rcHeader.bottom) / 2;
    DrawIconEx(m.dc, mg, hy - S(14), g_appIcon, S(28), S(28), 0, nullptr, DI_NORMAL);

    {
        Gdiplus::Graphics g(m.dc);
        PrepG(g);
        const float pill = (g_rcUrl.bottom - g_rcUrl.top) / 2.0f;
        if (g_urlFocus) {   // alone in tinta attorno al campo attivo
            RECT halo = g_rcUrl;
            InflateRect(&halo, S(3), S(3));
            FillRound(g, halo, pill + S(3), P.accent, 40);
        }
        FillRound(g, g_rcUrl, pill, P.field);
        StrokeRound(g, g_rcUrl, pill, g_urlFocus ? P.accent : P.border, g_urlFocus ? 1.6f : 1.0f);
        // separatore tra il campo e le icone interne
        RECT sep{ g_rcUrl.right - S(86), g_rcUrl.top + S(10), g_rcUrl.right - S(85), g_rcUrl.bottom - S(10) };
        FillRound(g, sep, 0, P.border);
    }
    RECT lr{ g_rcUrl.left + S(12), g_rcUrl.top, g_rcUrl.left + S(38), g_rcUrl.bottom };
    DrawGlyph(m.dc, G_LINK, lr, g_fIcon, g_urlFocus ? P.accent : P.muted);

    // barra di stato: contatori a destra, stato degli strumenti in accento
    int a = 0, q = 0, dn = 0, e = 0;
    for (auto& p : g_items) {
        if (IsActive(*p)) ++a;
        else if (p->st == St::Queued) ++q;
        else if (p->st == St::Done) ++dn;
        else if (p->st == St::Error) ++e;
    }
    std::wstring right = std::to_wstring(a) + L" in download   ·   " + std::to_wstring(q) + L" in coda   ·   " +
                         std::to_wstring(dn) + L" completati";
    if (e) right += L"   ·   " + std::to_wstring(e) + L" errori";
    if (!g_analyzeQueue.empty()) right += L"   ·   " + std::to_wstring(g_analyzeQueue.size()) + L" da analizzare";
    RECT fbr{};
    GetWindowRect(g_bFolder, &fbr);
    MapWindowPoints(nullptr, g_hwnd, (POINT*)&fbr, 2);
    int fx = fbr.right + S(16);
    RECT fr{ fx, g_rcFooter.top + 1, rc.right - mg, g_rcFooter.bottom };
    DrawText2(m.dc, right, fr, g_fSmall, P.muted, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (!g_footRight.empty()) {
        int rw = TextWidth(g_fSmall, right) + S(24);
        RECT rr{ fx, g_rcFooter.top + 1, std::max<LONG>(fx, fr.right - rw), g_rcFooter.bottom };
        DrawText2(m.dc, g_footRight, rr, g_fSmall, P.accent, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    // stato vuoto
    if (g_items.empty()) {
        int cy = (g_rcList.top + g_rcList.bottom) / 2;
        RECT ir{ 0, cy - S(110), rc.right, cy - S(30) };
        DrawGlyph(m.dc, G_DOWNLOAD, ir, g_fIconLg, g_dark ? P.border : Mix(P.border, P.muted, 0.35));
        RECT h1{ 0, cy - S(22), rc.right, cy + S(8) };
        DrawText2(m.dc, L"Nessun download", h1, g_fHead, P.text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        RECT h2{ 0, cy + S(12), rc.right, cy + S(34) };
        DrawText2(m.dc, L"Incolla un link nella barra in alto: YouTube, RaiPlay, Vimeo, SoundCloud, Twitch e migliaia di altri siti",
                  h2, g_fUi, P.muted, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        RECT h3{ S(20), cy + S(40), rc.right - S(20), cy + S(62) };
        DrawText2(m.dc, L"Invio = scarica col profilo a sinistra   ·   icona filtro = scegli video, audio e sottotitoli   ·   Ctrl+Maiusc+V = incolla e scarica",
                  h3, g_fSmall, P.muted, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    m.Blit(0, 0);
}

// ---------------------------------------------------------------------------
// Controlli
// ---------------------------------------------------------------------------
static HWND Btn(const wchar_t* text, int id) {
    HWND h = CreateWindowExW(0, WC_BUTTONW, text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10, g_hwnd,
                             (HMENU)(INT_PTR)id, g_inst, nullptr);
    SetWindowSubclass(h, BtnSubclass, 1, 0);
    return h;
}

static void ApplyFonts() {
    for (HWND h : { g_bPaste, g_bFormats, g_bDownload, g_bTheme, g_bSettings, g_bMore, g_bProfile, g_bFolder })
        SendMessageW(h, WM_SETFONT, (WPARAM)g_fUi, FALSE);
    SendMessageW(g_url, WM_SETFONT, (WPARAM)g_fUi, TRUE);
    SendMessageW(g_url, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    SendMessageW(g_list, LB_SETITEMHEIGHT, 0, ItemHeight());
}

static void UpdateChipTexts() {
    SetWindowTextW(g_bProfile, ProfileText().c_str());
    SetWindowTextW(g_bFolder, g_set.folder.c_str());
    for (HWND h : { g_bProfile, g_bFolder }) InvalidateRect(h, nullptr, FALSE);
}

static void CreateControls() {
    g_url = CreateWindowExW(0, WC_EDITW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 10, 10, g_hwnd,
                            (HMENU)(INT_PTR)IDC_URL, g_inst, nullptr);
    SendMessageW(g_url, EM_SETCUEBANNER, TRUE, (LPARAM)L"Incolla un link (o più link) e premi Invio");
    SendMessageW(g_url, EM_SETLIMITTEXT, 1 << 20, 0);
    g_bPaste    = Btn(L"", IDC_B_PASTE);
    g_bFormats  = Btn(L"", IDC_B_FORMATS);
    g_bDownload = Btn(L"Scarica", IDC_B_DOWNLOAD);
    g_bTheme    = Btn(L"", IDC_B_THEME);
    g_bSettings = Btn(L"", IDC_B_SETTINGS);
    g_bMore     = Btn(L"", IDC_B_MORE);
    g_bProfile  = Btn(L"", IDC_B_PROFILE);
    g_bFolder   = Btn(L"", IDC_B_FOLDER);
    g_list = CreateWindowExW(0, WC_LISTBOXW, L"",
                             WS_CHILD | WS_TABSTOP | WS_VSCROLL | LBS_OWNERDRAWFIXED | LBS_NOTIFY | LBS_EXTENDEDSEL |
                                 LBS_NOINTEGRALHEIGHT,
                             0, 0, 10, 10, g_hwnd, (HMENU)(INT_PTR)IDC_LIST, g_inst, nullptr);
    SetWindowSubclass(g_list, ListSubclass, 1, 0);

    // suggerimenti dei pulsanti
    HWND tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                               CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, g_hwnd, nullptr, g_inst, nullptr);
    auto addTip = [&](HWND h, const wchar_t* text) {
        TOOLINFOW ti{};
        ti.cbSize = sizeof(ti);
        ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        ti.hwnd = g_hwnd;
        ti.uId = (UINT_PTR)h;
        ti.lpszText = const_cast<LPWSTR>(text);
        SendMessageW(tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    };
    addTip(g_bPaste, L"Incolla dagli appunti");
    addTip(g_bFormats, L"Formati: analizza il link e scegli video, audio e sottotitoli");
    addTip(g_bDownload, L"Scarica con il formato selezionato (Invio)");
    addTip(g_bTheme, L"Tema: segue Windows / chiaro / scuro");
    addTip(g_bSettings, L"Impostazioni");
    addTip(g_bMore, L"Menu");
    addTip(g_bFolder, L"Cartella di destinazione: clic per aprirla o cambiarla");
    addTip(g_bProfile, L"Profilo di download: tipo, qualit\u00e0, formato, playlist e sottotitoli");
}

static void ApplyTheme() {
    ResolveTheme();
    SetTitleBarDark(g_hwnd, g_dark);
    SetWindowTheme(g_list, g_dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    SetWindowPos(g_hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    RedrawWindow(g_hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
}

// ---------------------------------------------------------------------------
// Menu
// ---------------------------------------------------------------------------
static void PopupBelow(HWND btn, HMENU m, bool rightAlign) {
    RECT r;
    GetWindowRect(btn, &r);
    TPMPARAMS tp{ sizeof(tp), r };
    TrackPopupMenuEx(m, (rightAlign ? TPM_RIGHTALIGN : TPM_LEFTALIGN) | TPM_TOPALIGN | TPM_VERTICAL,
                     rightAlign ? r.right : r.left, r.bottom + S(4), g_hwnd, &tp);
}

static void MoreMenu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_FILE_PASTE, L"Incolla link e scarica\tCtrl+Maiusc+V");
    AppendMenuW(m, MF_STRING, ID_FILE_OPENFOLDER, L"Apri cartella download\tCtrl+O");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_DL_CLEARDONE, L"Rimuovi i completati");
    AppendMenuW(m, MF_STRING, ID_DL_CANCELALL, L"Annulla tutti");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    UINT busy = g_toolBusy ? MF_GRAYED : 0;
    AppendMenuW(m, MF_STRING | busy, ID_TOOLS_UPD_YTDLP, L"Aggiorna / scarica yt-dlp");
    AppendMenuW(m, MF_STRING | busy, ID_TOOLS_GET_FFMPEG, L"Scarica / aggiorna ffmpeg");
    AppendMenuW(m, MF_STRING | busy, ID_TOOLS_GET_DENO, L"Scarica / aggiorna Deno");
    AppendMenuW(m, MF_STRING, ID_TOOLS_OPENBIN, L"Apri cartella strumenti");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_FILE_SETTINGS, L"Impostazioni...");
    AppendMenuW(m, MF_STRING, ID_HELP_GITHUB, L"Stackly su GitHub");
    AppendMenuW(m, MF_STRING, ID_HELP_ISSUES, L"Segnala un problema...");
    AppendMenuW(m, MF_STRING, ID_HELP_ABOUT, L"Informazioni su Stackly");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_TRAY_EXIT, L"Esci");
    PopupBelow(g_bMore, m, true);
    DestroyMenu(m);
}

static HMENU ChoiceSub(const Choice* items, int n, int base, int current) {
    HMENU m = CreatePopupMenu();
    for (int i = 0; i < n; ++i) {
        std::wstring t = items[i].label;
        if (items[i].hint) t += std::wstring(L"\t") + items[i].hint;
        AppendMenuW(m, MF_STRING, base + i, t.c_str());
    }
    CheckMenuRadioItem(m, base, base + n - 1, base + current, MF_BYCOMMAND);
    return m;
}

// Menu del profilo: in alto il tipo (cosa scaricare), sotto solo le opzioni che hanno senso per quel tipo
static void ProfileMenu() {
    const Preset& p = g_set.preset;
    const int k = p.kind;
    HMENU m = CreatePopupMenu();
    auto title = [&](const wchar_t* t) { AppendMenuW(m, MF_STRING | MF_GRAYED, 0, t); };
    auto sub = [&](const wchar_t* label, const wchar_t* cur, HMENU s, bool enabled = true) {
        AppendMenuW(m, MF_POPUP | (enabled ? 0 : MF_GRAYED), (UINT_PTR)s, (std::wstring(label) + L"\t" + cur).c_str());
    };
    auto check = [&](UINT id, const wchar_t* label, bool on, bool enabled = true) {
        AppendMenuW(m, MF_STRING | (on ? MF_CHECKED : 0) | (enabled ? 0 : MF_GRAYED), id, label);
    };

    title(L"COSA SCARICARE");
    for (int kind : kKindOrder) {
        std::wstring t = std::wstring(kKinds[kind].label) + L"\t" + kKinds[kind].hint;
        AppendMenuW(m, MF_STRING, ID_KIND_BASE + kind, t.c_str());
    }
    CheckMenuRadioItem(m, ID_KIND_BASE, ID_KIND_BASE + CountOf(kKinds) - 1, ID_KIND_BASE + k, MF_BYCOMMAND);
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);

    if (KindIsVideo(k)) {
        title(k == DK_VIDEO ? L"VIDEO + AUDIO" : L"SOLO VIDEO");
        sub(L"Qualità", kVideoQ[p.vq].value, ChoiceSub(kVideoQ, CountOf(kVideoQ), ID_VQ_BASE, p.vq));
        sub(L"Contenitore", kContainers[p.container].value,
            ChoiceSub(kContainers, CountOf(kContainers), ID_CONT_BASE, p.container));
        if (k == DK_VIDEO) check(ID_OPT_SUBS, L"Sottotitoli nel video\tlingue nelle Impostazioni", g_set.subs);
    } else if (k == DK_AUDIO) {
        title(L"SOLO AUDIO");
        sub(L"Formato", kAudioFmts[p.afmt].value, ChoiceSub(kAudioFmts, CountOf(kAudioFmts), ID_AFMT_BASE, p.afmt));
        sub(L"Qualità", p.afmt == 2 ? L"come l'originale" : kAudioQ[p.aq].value,
            ChoiceSub(kAudioQ, CountOf(kAudioQ), ID_AQ_BASE, p.aq), p.afmt != 2);
    } else if (k == DK_SUBS) {
        title(L"SOLO SOTTOTITOLI");
        sub(L"Formato", kSubFmts[p.subfmt].value, ChoiceSub(kSubFmts, CountOf(kSubFmts), ID_SUBF_BASE, p.subfmt));
        check(ID_OPT_AUTOSUBS, L"Anche quelli generati automaticamente", g_set.autoSubs);
        AppendMenuW(m, MF_STRING | MF_GRAYED, 0, (L"Lingue\t" + g_set.subLangs).c_str());
    } else {
        title(L"SCEGLI OGNI VOLTA");
        AppendMenuW(m, MF_STRING | MF_GRAYED, 0, L"Analizza il link: scegli tu video, audio e sottotitoli");
    }
    if (k != DK_MANUAL) {
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        check(ID_OPT_PLAYLIST, L"Playlist intera\tse il link è una playlist o un canale", g_set.playlist);
    }
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_OPT_FORMATS, L"Scegli i formati per questo link...");
    PopupBelow(g_bProfile, m, false);
    DestroyMenu(m);   // distrugge anche i sottomenu
}

static void FolderMenu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_FOLDER_OPEN, L"Apri cartella\tCtrl+O");
    AppendMenuW(m, MF_STRING, ID_FOLDER_CHANGE, L"Cambia cartella...");
    RECT r;
    GetWindowRect(g_bFolder, &r);
    TrackPopupMenu(m, TPM_LEFTALIGN | TPM_BOTTOMALIGN, r.left, r.top - S(4), 0, g_hwnd, nullptr);
    DestroyMenu(m);
}

static void ThemeMenu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_THEME_SYSTEM, L"Segui Windows");
    AppendMenuW(m, MF_STRING, ID_THEME_LIGHT, L"Chiaro");
    AppendMenuW(m, MF_STRING, ID_THEME_DARK, L"Scuro");
    CheckMenuRadioItem(m, ID_THEME_SYSTEM, ID_THEME_DARK, ID_THEME_SYSTEM + g_set.theme, MF_BYCOMMAND);
    PopupBelow(g_bTheme, m, true);
    DestroyMenu(m);
}

static void ContextMenu(LPARAM lp) {
    POINT pt{ (short)LOWORD(lp), (short)HIWORD(lp) };
    if (lp == -1) {
        int caret = (int)SendMessageW(g_list, LB_GETCARETINDEX, 0, 0);
        RECT r{};
        SendMessageW(g_list, LB_GETITEMRECT, std::max(caret, 0), (LPARAM)&r);
        pt = { r.left + S(40), r.top + S(30) };
        ClientToScreen(g_list, &pt);
    } else {
        POINT cp = pt;
        ScreenToClient(g_list, &cp);
        LRESULT r = SendMessageW(g_list, LB_ITEMFROMPOINT, 0, MAKELPARAM(cp.x, cp.y));
        if (HIWORD(r)) return;
        int idx = (int)LOWORD(r);
        if (SendMessageW(g_list, LB_GETSEL, idx, 0) <= 0) {
            SendMessageW(g_list, LB_SETSEL, FALSE, -1);
            SendMessageW(g_list, LB_SETSEL, TRUE, idx);
        }
    }
    if (SendMessageW(g_list, LB_GETSELCOUNT, 0, 0) <= 0) return;
    HMENU menu = LoadMenuW(g_inst, MAKEINTRESOURCEW(IDR_LISTMENU));
    HMENU sub = GetSubMenu(menu, 0);
    TrackPopupMenu(sub, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, nullptr);
    DestroyMenu(menu);
}

// ---------------------------------------------------------------------------
// Uscita
// ---------------------------------------------------------------------------
static void SaveWindowState() {
    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    if (IsWindowVisible(g_hwnd) && GetWindowPlacement(g_hwnd, &wp)) {
        if (wp.showCmd == SW_SHOWMINIMIZED || wp.showCmd == SW_HIDE) wp.showCmd = SW_SHOWNORMAL;
        g_set.wp = wp;
        g_set.hasWp = true;
    }
    SaveSettings();
}

static void QuitApp(bool ask) {
    int active = 0;
    for (auto& p : g_items) if (IsActive(*p)) ++active;
    if (ask && active) {
        ShowMain();
        if (MessageBoxW(g_hwnd, L"Ci sono download in corso. Uscire e interromperli?\n\n"
                                L"I download segmentati riprendono da dove erano se riaggiungi lo stesso link con le stesse opzioni.",
                        kAppName, MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) != IDYES)
            return;
    }
    g_quitting = true;
    SaveWindowState();
    for (auto& p : g_items) if (IsActive(*p)) CancelItem(*p);
    DestroyWindow(g_hwnd);
}

// ---------------------------------------------------------------------------
// Finestra principale
// ---------------------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static UINT s_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    if (s_taskbarCreated && msg == s_taskbarCreated) { g_trayAdded = false; TrayAdd(); return 0; }

    switch (msg) {
    case WM_CREATE:
        g_hwnd = hwnd;
        g_dpi = WndDpi(hwnd);
        SetTitleBarDark(hwnd, g_dark);
        MakeFonts();
        CreateControls();
        ApplyFonts();
        UpdateChipTexts();
        SetWindowTheme(g_list, g_dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
        Layout();
        TrayAdd();
        SetTimer(hwnd, kRepaintTimer, kRepaintMs, nullptr);
        PostMessageW(hwnd, WM_APP_FIRSTRUN, 0, 0);
        return 0;

    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) Layout();
        return 0;

    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize = { S(820), S(480) };
        return 0;

    case WM_DPICHANGED: {
        g_dpi = HIWORD(wp);
        MakeFonts();
        ApplyFonts();
        auto* r = reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        Layout();
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        Paint(dc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CTLCOLOREDIT:
        if ((HWND)lp == g_url) {
            SetTextColor((HDC)wp, P.text);
            SetBkColor((HDC)wp, P.field);
            return (LRESULT)g_brField;
        }
        break;
    case WM_CTLCOLORLISTBOX:
        return (LRESULT)g_brBg;

    case WM_MEASUREITEM: {
        auto* mi = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
        if (mi->CtlType == ODT_LISTBOX) { mi->itemHeight = ItemHeight(); return TRUE; }
        break;
    }

    case WM_DRAWITEM: {
        auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if (dis->CtlType == ODT_BUTTON) { DrawButton(dis); return TRUE; }
        if (dis->CtlType == ODT_LISTBOX) {
            if ((int)dis->itemID < 0) return TRUE;
            Item* it = FindItem((int)dis->itemData);
            if (!it) { FillRect(dis->hDC, &dis->rcItem, g_brBg); return TRUE; }
            bool sel = (dis->itemState & ODS_SELECTED) != 0;
            bool hot = g_hotItem == (int)dis->itemID;
            DrawCard(dis->hDC, dis->rcItem, *it, sel, hot, hot ? g_hotPart : 0, GetFocus() == g_list);
            it->dirty = false;
            return TRUE;
        }
        break;
    }

    case WM_TIMER:
        if (wp == kRepaintTimer) {
            // Ridisegno raggruppato: ogni 150 ms solo le card cambiate
            bool any = false;
            for (auto& p : g_items) if (p->dirty) { InvalidateItem(p->id); any = true; }
            if (any) UpdateStatus();
            static int tick = 0;
            if (++tick % 8 == 0) UpdateTrayTip();
        }
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(wp);
        int code = HIWORD(wp);
        if (id >= ID_SUBF_BASE && id < ID_SUBF_BASE + CountOf(kSubFmts)) {
            g_set.preset.subfmt = id - ID_SUBF_BASE;
            UpdateChipTexts(); Layout(); SaveSettings();
            return 0;
        }
        if (id >= ID_KIND_BASE && id < ID_PKCONT_BASE) {
            int grp = (id - ID_KIND_BASE) / 100, v = (id - ID_KIND_BASE) % 100;
            Preset& p = g_set.preset;
            if (grp == 0) p.kind = std::clamp(v, 0, CountOf(kKinds) - 1);
            else if (grp == 1) p.vq = std::clamp(v, 0, CountOf(kVideoQ) - 1);
            else if (grp == 2) p.container = std::clamp(v, 0, 1);
            else if (grp == 3) p.afmt = std::clamp(v, 0, CountOf(kAudioFmts) - 1);
            else if (grp == 4) p.aq = std::clamp(v, 0, CountOf(kAudioQ) - 1);
            UpdateChipTexts();
            Layout();
            SaveSettings();
            return 0;
        }
        switch (id) {
        case IDOK:   // Invio (IsDialogMessage)
            if (GetFocus() == g_list) OnListCommand(ID_DL_OPENFILE);
            else if (GetFocus() == g_url || GetFocus() == g_bDownload) AddUrls(GetText(g_url));
            return 0;
        case IDCANCEL:
            return 0;
        case IDC_URL:
            if (code == EN_SETFOCUS || code == EN_KILLFOCUS) {
                g_urlFocus = code == EN_SETFOCUS;
                RECT r = g_rcUrl;
                InflateRect(&r, 2, 2);
                InvalidateRect(hwnd, &r, FALSE);
            }
            return 0;
        case IDC_LIST:
            if (code == LBN_DBLCLK) OnListCommand(ID_DL_OPENFILE);
            else if (code == LBN_SETFOCUS || code == LBN_KILLFOCUS) InvalidateRect(g_list, nullptr, FALSE);
            return 0;
        case IDC_B_DOWNLOAD:
            AddUrls(GetText(g_url));
            return 0;
        case IDC_B_FORMATS: {
            std::wstring t = GetText(g_url);
            if (Trim(t).empty()) t = ClipboardText();
            if (ExtractUrls(t).empty()) {
                MessageBoxW(hwnd, L"Incolla prima un link nella barra in alto.", kAppName, MB_ICONINFORMATION);
                SetFocus(g_url);
            } else {
                AddUrls(t, true);
            }
            return 0;
        }
        case IDC_B_PASTE: {
            std::wstring t = Trim(ClipboardText());
            if (!t.empty()) { SetWindowTextW(g_url, t.c_str()); SendMessageW(g_url, EM_SETSEL, t.size(), t.size()); }
            SetFocus(g_url);
            return 0;
        }
        case IDC_B_MORE:   MoreMenu(); return 0;
        case IDC_B_THEME:  ThemeMenu(); return 0;
        case IDC_B_SETTINGS: SendMessageW(hwnd, WM_COMMAND, ID_FILE_SETTINGS, 0); return 0;
        case IDC_B_PROFILE:  ProfileMenu(); return 0;
        case ID_OPT_PLAYLIST: g_set.playlist = !g_set.playlist; UpdateChipTexts(); Layout(); SaveSettings(); return 0;
        case ID_OPT_SUBS:     g_set.subs = !g_set.subs; UpdateChipTexts(); Layout(); SaveSettings(); return 0;
        case ID_OPT_AUTOSUBS: g_set.autoSubs = !g_set.autoSubs; SaveSettings(); return 0;
        case ID_OPT_FORMATS:  SendMessageW(hwnd, WM_COMMAND, IDC_B_FORMATS, 0); return 0;
        case IDC_B_FOLDER:    FolderMenu(); return 0;
        case ID_FOLDER_OPEN:  SendMessageW(hwnd, WM_COMMAND, ID_FILE_OPENFOLDER, 0); return 0;
        case ID_FOLDER_CHANGE: {
            std::wstring f = g_set.folder;
            if (PickFolder(hwnd, f)) {
                g_set.folder = f;
                UpdateChipTexts();
                Layout();
                SaveSettings();
            }
            return 0;
        }
        case ID_THEME_SYSTEM: case ID_THEME_LIGHT: case ID_THEME_DARK:
            g_set.theme = id - ID_THEME_SYSTEM;
            SaveSettings();
            ApplyTheme();
            return 0;
        case ID_FILE_PASTE:
            ShowMain();
            AddUrls(ClipboardText());
            return 0;
        case ID_FILE_OPENFOLDER:
            if (EnsureFolder()) ShellExecuteW(hwnd, L"open", g_set.folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        case ID_FILE_SETTINGS:
            DialogBoxParamW(g_inst, MAKEINTRESOURCEW(IDD_SETTINGS), hwnd, SettingsProc, 0);
            PumpQueue();
            return 0;
        case ID_DL_CANCEL: case ID_DL_RETRY: case ID_DL_REMOVE:
        case ID_DL_OPENFILE: case ID_DL_OPENITEMFOLDER: case ID_DL_COPYURL:
            OnListCommand(id);
            return 0;
        case ID_DL_CLEARDONE:
            ClearDone();
            return 0;
        case ID_DL_CANCELALL:
            for (auto& p : g_items) CancelItem(*p);
            g_analyzeQueue.clear();
            UpdateStatus();
            return 0;
        case ID_TOOLS_UPD_YTDLP:  RunTool(TT_YTDLP);  return 0;
        case ID_TOOLS_GET_FFMPEG: RunTool(TT_FFMPEG); return 0;
        case ID_TOOLS_GET_DENO:   RunTool(TT_DENO);   return 0;
        case ID_TOOLS_OPENBIN:
            ShellExecuteW(hwnd, L"open", BinDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        case ID_HELP_GITHUB:
            ShellExecuteW(hwnd, L"open", kRepoUrl, nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        case ID_HELP_ISSUES:
            ShellExecuteW(hwnd, L"open", kIssuesUrl, nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        case ID_HELP_ABOUT:
            DialogBoxParamW(g_inst, MAKEINTRESOURCEW(IDD_ABOUT), hwnd, AboutProc, 0);
            return 0;
        case ID_TRAY_SHOW:
            ShowMain();
            return 0;
        case ID_TRAY_EXIT:
            QuitApp(true);
            return 0;
        }
        break;
    }

    case WM_CONTEXTMENU:
        if ((HWND)wp == g_list) { ContextMenu(lp); return 0; }
        break;

    case WM_APP_TRAY:
        switch (LOWORD(lp)) {
        case NIN_SELECT:
        case NIN_KEYSELECT:
            if (IsWindowVisible(hwnd) && !IsIconic(hwnd) && GetForegroundWindow() == hwnd) {
                SaveWindowState();
                ShowWindow(hwnd, SW_HIDE);
            } else {
                ShowMain();
            }
            break;
        case WM_CONTEXTMENU:
            TrayMenu();
            break;
        case NIN_BALLOONUSERCLICK:
            ShowMain();
            break;
        }
        return 0;

    case WM_APP_LINE: {
        std::unique_ptr<std::wstring> s(reinterpret_cast<std::wstring*>(lp));
        OnLine((int)wp, *s);
        return 0;
    }
    case WM_APP_DONE:
        OnDone((int)wp, (DWORD)lp);
        return 0;
    case WM_APP_FALLBACK:
        OnFallback((int)wp);
        return 0;
    case WM_APP_THUMB:
        OnThumb(reinterpret_cast<ThumbMsg*>(lp));
        return 0;
    case WM_APP_ANALYZED:
        OnAnalyzed(reinterpret_cast<AnalyzeResult*>(lp));
        return 0;
    case WM_APP_TOOLSTATUS: {
        std::unique_ptr<std::wstring> s(reinterpret_cast<std::wstring*>(lp));
        g_footRight = *s;
        UpdateStatus();
        return 0;
    }
    case WM_APP_TOOLDONE: {
        std::unique_ptr<std::wstring> s(reinterpret_cast<std::wstring*>(lp));
        InterlockedExchange(&g_toolBusy, 0);
        g_footRight.clear();
        UpdateStatus();
        if (IsWindowVisible(hwnd)) MessageBoxW(hwnd, s->empty() ? L"Fatto." : s->c_str(), L"Strumenti", MB_ICONINFORMATION);
        else ShowTrayBalloon(L"Strumenti aggiornati", L"yt-dlp / ffmpeg / Deno pronti.");
        PumpQueue();
        return 0;
    }
    case WM_APP_FIRSTRUN: {
        bool noYt = ResolveTool(g_set.ytdlp, L"yt-dlp.exe").empty();
        bool noFf = ResolveTool(g_set.ffmpeg, L"ffmpeg.exe").empty();
        bool noDn = ResolveTool(L"", L"deno.exe").empty();
        if (noYt || noFf) {
            std::wstring q = L"Per funzionare servono questi componenti, che non ho trovato:\n\n";
            if (noYt) q += L"  • yt-dlp (estrazione dei link)\n";
            if (noFf) q += L"  • ffmpeg (unione audio/video, MP3, sottotitoli)\n";
            if (noDn) q += L"  • Deno (necessario a yt-dlp per YouTube)\n";
            q += L"\nScaricarli ora? Restano nella cartella di Stackly.";
            if (MessageBoxW(hwnd, q.c_str(), kAppName, MB_ICONQUESTION | MB_YESNO) == IDYES)
                RunTool((noYt ? TT_YTDLP : 0) | (noFf ? TT_FFMPEG : 0) | (noDn ? TT_DENO : 0));
        }
        return 0;
    }

    case WM_SETTINGCHANGE:
        if (lp && !wcscmp(reinterpret_cast<const wchar_t*>(lp), L"ImmersiveColorSet") && g_set.theme == 0) ApplyTheme();
        break;

    case WM_CLOSE:
        if (g_set.tray && g_trayAdded && !g_quitting) {
            SaveWindowState();
            ShowWindow(hwnd, SW_HIDE);
            static bool told = false;
            if (!told) {
                told = true;
                ShowTrayBalloon(L"Stackly è ancora attivo", L"Continua nell'area di notifica. Clic destro sull'icona per uscire.");
            }
            return 0;
        }
        QuitApp(true);
        return 0;

    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (wp) {
            g_quitting = true;
            SaveWindowState();
            for (auto& p : g_items) if (IsActive(*p)) CancelItem(*p);
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, kRepaintTimer);
        TrayRemove();
        for (auto& p : g_items) CloseItemHandles(*p);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// Istanza singola: un secondo avvio porta in primo piano quella gia' aperta
static void ActivateExisting() {
    HWND h = FindWindowW(kClass, nullptr);
    if (!h) return;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    AllowSetForegroundWindow(pid);
    PostMessageW(h, WM_COMMAND, ID_TRAY_SHOW, 0);
}

// Percorso dell'eseguibile di un'altra istanza (vuoto se non determinabile)
static std::wstring OtherInstancePath(HWND h, DWORD& pid) {
    pid = 0;
    GetWindowThreadProcessId(h, &pid);
    std::wstring path;
    if (HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        wchar_t buf[MAX_PATH * 2];
        DWORD n = (DWORD)std::size(buf);
        if (QueryFullProcessImageNameW(p, 0, buf, &n)) path.assign(buf, n);
        CloseHandle(p);
    }
    return path;
}

// Se e' gia' aperta una copia di Stackly da un altro percorso (es. versione vecchia rimasta nell'area di
// notifica), chiede se chiuderla e avviare questa; altrimenti porta in primo piano quella aperta.
// Restituisce true se questa istanza deve proseguire.
static bool HandleOtherInstance() {
    HWND h = FindWindowW(kClass, nullptr);
    if (!h) return true;   // l'altra istanza si sta chiudendo
    DWORD pid = 0;
    std::wstring other = OtherInstancePath(h, pid);
    wchar_t self[MAX_PATH * 2];
    GetModuleFileNameW(nullptr, self, (DWORD)std::size(self));
    if (other.empty() || _wcsicmp(other.c_str(), self) == 0) { ActivateExisting(); return false; }
    std::wstring q = L"\u00c8 gi\u00e0 aperta un'altra copia di Stackly (anche nell'area di notifica):\n\n" + other +
                     L"\n\nChiuderla e avviare questa?\n" + self;
    if (MessageBoxW(nullptr, q.c_str(), kAppName, MB_ICONQUESTION | MB_YESNO | MB_SETFOREGROUND) != IDYES) {
        ActivateExisting();
        return false;
    }
    HANDLE proc = OpenProcess(SYNCHRONIZE, FALSE, pid);
    AllowSetForegroundWindow(pid);
    PostMessageW(h, WM_COMMAND, ID_TRAY_EXIT, 0);   // se ha download in corso chiede conferma
    DWORD w = proc ? WaitForSingleObject(proc, 60000) : WAIT_FAILED;
    if (proc) CloseHandle(proc);
    if (w != WAIT_OBJECT_0) {
        MessageBoxW(nullptr, L"L'altra copia di Stackly \u00e8 ancora aperta. Chiudila dall'area di notifica (clic destro > Esci) e riprova.",
                    kAppName, MB_ICONINFORMATION);
        return false;
    }
    return true;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show) {
    g_inst = inst;
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\Stackly.SingleInstance");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS && !HandleOtherInstance()) {
        CloseHandle(mutex);
        return 0;
    }
    HeapSetInformation(nullptr, HeapEnableTerminationOnCorruption, nullptr, 0);
    HRESULT coHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_UPDOWN_CLASS | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    Gdiplus::GdiplusStartupInput gsi;
    ULONG_PTR gdipToken = 0;
    Gdiplus::GdiplusStartup(&gdipToken, &gsi, nullptr);

    InitPaths();
    LoadSettings();
    if (FontExists(L"Segoe Fluent Icons")) g_iconFace = L"Segoe Fluent Icons";
    g_connSlots = CreateSemaphoreW(nullptr, kGlobalConnections, kGlobalConnections, nullptr);
    ResolveTheme();   // prima della finestra: menu e dialoghi gia' nel tema giusto

    g_appIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 256, 256, LR_DEFAULTCOLOR);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                   GetSystemMetrics(SM_CYSMICON), 0);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, kClass, kAppName, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                                1200, 720, nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;
    if (g_set.hasWp) {
        WINDOWPLACEMENT wp = g_set.wp;
        wp.length = sizeof(wp);
        wp.showCmd = (show == SW_SHOWMINNOACTIVE || show == SW_MINIMIZE || show == SW_SHOWMINIMIZED) ? (UINT)show : g_set.wp.showCmd;
        SetWindowPlacement(hwnd, &wp);
    } else {
        UINT dpi = WndDpi(hwnd);
        SetWindowPos(hwnd, nullptr, 0, 0, MulDiv(1200, (int)dpi, 96), MulDiv(720, (int)dpi, 96), SWP_NOMOVE | SWP_NOZORDER);
        ShowWindow(hwnd, show);
    }
    UpdateWindow(hwnd);
    SetFocus(g_url);

    HACCEL acc = LoadAcceleratorsW(inst, MAKEINTRESOURCEW(IDR_ACCEL));
    MSG m{};
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (TranslateAcceleratorW(hwnd, acc, &m)) continue;
        if (IsDialogMessageW(hwnd, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    g_items.clear();   // libera le miniature GDI+ prima dello spegnimento
    Gdiplus::GdiplusShutdown(gdipToken);
    if (SUCCEEDED(coHr)) CoUninitialize();
    if (mutex) CloseHandle(mutex);
    // ExitProcess ferma subito i thread di lavoro ancora vivi (download annullati, miniature)
    // prima dei distruttori statici: niente crash in chiusura.
    ExitProcess((UINT)m.wParam);
}
