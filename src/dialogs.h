// Stackly - finestra Formati (scelta manuale di video/audio/sottotitoli) e Impostazioni
#pragma once
#include "ui.h"

// ---------------------------------------------------------------------------
// Tema dei dialoghi
// ---------------------------------------------------------------------------
static BOOL CALLBACK ThemeChildProc(HWND h, LPARAM) {
    wchar_t cls[64];
    GetClassNameW(h, cls, 64);
    LONG style = GetWindowLongW(h, GWL_STYLE);
    if (!_wcsicmp(cls, WC_BUTTONW)) {
        LONG t = style & BS_TYPEMASK;
        if (t == BS_OWNERDRAW) return TRUE;
        if (t == BS_AUTOCHECKBOX || t == BS_CHECKBOX || t == BS_AUTORADIOBUTTON || t == BS_RADIOBUTTON)
            SetWindowTheme(h, g_dark ? L"" : nullptr, g_dark ? L"" : nullptr);  // classico: il colore del testo segue WM_CTLCOLORSTATIC
        else
            SetWindowTheme(h, g_dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    } else if (!_wcsicmp(cls, WC_EDITW)) {
        SetWindowTheme(h, g_dark ? L"DarkMode_CFD" : L"Explorer", nullptr);
    } else if (!_wcsicmp(cls, UPDOWN_CLASSW) || !_wcsicmp(cls, WC_LISTVIEWW)) {
        SetWindowTheme(h, g_dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
        if (!_wcsicmp(cls, WC_LISTVIEWW)) {
            HWND hdr = (HWND)SendMessageW(h, LVM_GETHEADER, 0, 0);
            if (hdr) SetWindowTheme(hdr, g_dark ? L"DarkMode_ItemsView" : L"Explorer", nullptr);
            SendMessageW(h, LVM_SETBKCOLOR, 0, P.surface);
            SendMessageW(h, LVM_SETTEXTBKCOLOR, 0, P.surface);
            SendMessageW(h, LVM_SETTEXTCOLOR, 0, P.text);
        }
    }
    InvalidateRect(h, nullptr, TRUE);
    return TRUE;
}

// ---------------------------------------------------------------------------
// Cornici arrotondate disegnate dal dialogo attorno a campi di testo e liste
// (al posto della cornice incassata di sistema, che nel tema scuro stona)
// ---------------------------------------------------------------------------
static const wchar_t* kFrameProp = L"stk.frame";        // 1 = campo di testo, 2 = lista
static const wchar_t* kPartnerProp = L"stk.partner";    // controllo da includere nella cornice (spin)

static void MarkFrame(HWND h, int kind, HWND partner = nullptr) {
    if (!h) return;
    SetPropW(h, kFrameProp, (HANDLE)(INT_PTR)kind);
    if (partner) SetPropW(h, kPartnerProp, partner);
    LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE), st = GetWindowLongPtrW(h, GWL_STYLE);
    SetWindowLongPtrW(h, GWL_EXSTYLE, ex & ~(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE));
    SetWindowLongPtrW(h, GWL_STYLE, st & ~WS_BORDER);
    SetWindowPos(h, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    if (kind == 1) {
        SendMessageW(h, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(S(2), S(2)));
        // Senza cornice di sistema il testo di un EDIT si appoggia in alto: il controllo viene ridotto
        // all'altezza della riga e centrato; la cornice disegnata conserva l'altezza originale.
        RECT r;
        GetWindowRect(h, &r);
        MapWindowPoints(nullptr, GetParent(h), (POINT*)&r, 2);
        int oh = r.bottom - r.top;
        SetPropW(h, L"stk.fh", (HANDLE)(INT_PTR)oh);
        HDC dc = GetDC(h);
        HGDIOBJ old = SelectObject(dc, (HFONT)SendMessageW(h, WM_GETFONT, 0, 0));
        TEXTMETRICW tm{};
        GetTextMetricsW(dc, &tm);
        SelectObject(dc, old);
        ReleaseDC(h, dc);
        int th = tm.tmHeight + 2;
        if (th > 0 && th < oh) {
            int cy = (r.top + r.bottom) / 2;
            SetWindowPos(h, nullptr, r.left, cy - th / 2, r.right - r.left, th, SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
}

static RECT FrameRect(HWND dlg, HWND h) {
    RECT r;
    GetWindowRect(h, &r);
    if (HWND p = (HWND)GetPropW(h, kPartnerProp)) {
        RECT pr;
        GetWindowRect(p, &pr);
        UnionRect(&r, &r, &pr);
    }
    MapWindowPoints(nullptr, dlg, (POINT*)&r, 2);
    if ((INT_PTR)GetPropW(h, kFrameProp) == 1) {
        // cornice centrata sul testo, alta quanto il campo originale
        RECT e;
        GetWindowRect(h, &e);
        MapWindowPoints(nullptr, dlg, (POINT*)&e, 2);
        int fh = (int)(INT_PTR)GetPropW(h, L"stk.fh");
        if (fh <= 0) fh = e.bottom - e.top;
        int cy = (e.top + e.bottom) / 2;
        r.top = cy - fh / 2 - S(3);
        r.bottom = cy + (fh - fh / 2) + S(3);
        r.left -= S(6);
        r.right += S(6);
    } else {
        InflateRect(&r, S(1), S(1));
    }
    return r;
}

struct FramePaint { HWND dlg; HDC dc; };
static BOOL CALLBACK PaintFrameProc(HWND h, LPARAM lp) {
    auto* fp = reinterpret_cast<FramePaint*>(lp);
    INT_PTR kind = (INT_PTR)GetPropW(h, kFrameProp);
    if (!kind || !IsWindowVisible(h)) return TRUE;
    RECT r = FrameRect(fp->dlg, h);
    bool focus = GetFocus() == h;
    Gdiplus::Graphics g(fp->dc);
    PrepG(g);
    float rad = (float)(kind == 1 ? S(3) : S(4));
    FillRound(g, r, rad, kind == 1 ? P.field : P.surface);
    StrokeRound(g, r, rad, focus ? P.accent : (g_dark ? Mix(P.border, P.field, 0.2) : P.border), focus ? 1.6f : 1.0f);
    return TRUE;
}

static void PaintDialogFrames(HWND dlg, HDC hdc) {
    RECT rc;
    GetClientRect(dlg, &rc);
    FillRect(hdc, &rc, g_brBg);
    FramePaint fp{ dlg, hdc };
    EnumChildWindows(dlg, PaintFrameProc, (LPARAM)&fp);
}

static void InvalidateFrame(HWND dlg, HWND h) {
    if (!h || !GetPropW(h, kFrameProp)) return;
    RECT r = FrameRect(dlg, h);
    InflateRect(&r, 2, 2);
    InvalidateRect(dlg, &r, TRUE);
}

// Gestione comune: sfondo + cornici e ridisegno al cambio di fuoco. Restituisce true se ha gestito il messaggio.
static bool FrameMessages(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND:
        PaintDialogFrames(dlg, (HDC)wp);
        SetWindowLongPtrW(dlg, DWLP_MSGRESULT, 1);
        return true;
    case WM_COMMAND: {
        int code = HIWORD(wp);
        if (code == EN_SETFOCUS || code == EN_KILLFOCUS) InvalidateFrame(dlg, (HWND)lp);
        return false;
    }
    case WM_NOTIFY: {
        auto* nh = reinterpret_cast<NMHDR*>(lp);
        if (nh->code == NM_SETFOCUS || nh->code == NM_KILLFOCUS) InvalidateFrame(dlg, nh->hwndFrom);
        return false;
    }
    case WM_LBUTTONDOWN: {   // clic nella cornice ma fuori dal campo: attiva il campo
        POINT pt{ (short)LOWORD(lp), (short)HIWORD(lp) };
        for (HWND c = GetWindow(dlg, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
            if ((INT_PTR)GetPropW(c, kFrameProp) != 1 || !IsWindowVisible(c)) continue;
            RECT r = FrameRect(dlg, c);
            if (PtInRect(&r, pt)) {
                SetFocus(c);
                SendMessageW(c, EM_SETSEL, (WPARAM)-1, -1);   // cursore in fondo al testo
                return true;
            }
        }
        return false;
    }
    }
    return false;
}

// Caselle di spunta delle liste disegnate nello stile dell'app (le predefinite sono bianche anche nel tema scuro)
static HIMAGELIST MakeCheckImages() {
    const int sz = S(kCheckSize) + 2;   // 1 px di margine attorno alla casella
    HIMAGELIST il = ImageList_Create(sz, sz, ILC_COLOR32, 2, 0);
    for (int on = 0; on < 2; ++on) {
        Gdiplus::Bitmap bmp(sz, sz, PixelFormat32bppPARGB);
        {
            Gdiplus::Graphics g(&bmp);
            PrepG(g);
            g.Clear(Gdiplus::Color(0, 0, 0, 0));
            RECT r{ 1, 1, sz - 1, sz - 1 };
            DrawCheckBox(g, r, on != 0, false, false);
        }
        HBITMAP hb = nullptr;
        bmp.GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &hb);
        if (hb) { ImageList_Add(il, hb, nullptr); DeleteObject(hb); }
    }
    return il;
}

static void UseAppCheckboxes(HWND lv) {
    HIMAGELIST old = (HIMAGELIST)SendMessageW(lv, LVM_SETIMAGELIST, LVSIL_STATE, (LPARAM)MakeCheckImages());
    if (old) ImageList_Destroy(old);
}

// Frecce su/giu' dei campi numerici disegnate dall'app (quelle di sistema restano chiare nel tema scuro)
static LRESULT CALLBACK SpinSubclass(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, h, 0 };
        TrackMouseEvent(&t);
        InvalidateRect(h, nullptr, FALSE);
        break;
    }
    case WM_MOUSELEAVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED: {
        LRESULT r = DefSubclassProc(h, msg, wp, lp);
        InvalidateRect(h, nullptr, FALSE);
        return r;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        int w = rc.right, hh = rc.bottom;
        MemDC m(dc, w, hh);
        RECT full{ 0, 0, w, hh };
        HBRUSH b = CreateSolidBrush(P.field);
        FillRect(m.dc, &full, b);
        DeleteObject(b);
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(h, &pt);
        bool inside = PtInRect(&full, pt) != 0;
        bool down = (GetKeyState(VK_LBUTTON) & 0x8000) && GetCapture() == h;
        RECT up{ 0, 0, w, hh / 2 }, dn{ 0, hh / 2, w, hh };
        {
            Gdiplus::Graphics g(m.dc);
            PrepG(g);
            for (int i = 0; i < 2; ++i) {
                const RECT& r = i ? dn : up;
                if (inside && PtInRect(&r, pt)) {
                    RECT f = r;
                    InflateRect(&f, -1, -1);
                    FillRound(g, f, (float)S(3), P.accent, down ? 70 : 38);
                }
            }
        }
        DrawGlyph(m.dc, 0xE70E, up, g_fIconSm, (inside && PtInRect(&up, pt)) ? P.accent : P.muted);   // su
        DrawGlyph(m.dc, 0xE70D, dn, g_fIconSm, (inside && PtInRect(&dn, pt)) ? P.accent : P.muted);   // giu'
        m.Blit(0, 0);
        EndPaint(h, &ps);
        return 0;
    }
    }
    return DefSubclassProc(h, msg, wp, lp);
}

// Converte un pulsante di sistema (spunta o pulsante) in uno disegnato dall'app
static void OwnerDrawButton(HWND h, bool isCheck) {
    if (!h) return;
    if (isCheck) SetWindowLongPtrW(h, GWLP_USERDATA, SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED);
    LONG_PTR st = GetWindowLongPtrW(h, GWL_STYLE);
    SetWindowLongPtrW(h, GWL_STYLE, (st & ~(LONG_PTR)BS_TYPEMASK) | BS_OWNERDRAW);
    SendMessageW(h, WM_SETFONT, (WPARAM)g_fUi, FALSE);
    SetWindowSubclass(h, BtnSubclass, 1, 0);
    InvalidateRect(h, nullptr, TRUE);
}

static void ThemeDialog(HWND dlg) {
    SetTitleBarDark(dlg, g_dark);
    EnumChildWindows(dlg, ThemeChildProc, 0);
    InvalidateRect(dlg, nullptr, TRUE);
}

// Gestione comune dei colori nei dialoghi; restituisce 0 se il messaggio non e' di colore
static INT_PTR DialogColors(UINT msg, WPARAM wp, LPARAM lp, HBRUSH bg, COLORREF bgColor) {
    switch (msg) {
    case WM_CTLCOLORDLG:
        return (INT_PTR)bg;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC dc = (HDC)wp;
        int id = GetDlgCtrlID((HWND)lp);
        bool muted = id == IDC_PK_LBLV || id == IDC_PK_LBLA || id == IDC_PK_LBLS || id == IDC_PK_INFO || id == IDC_PK_SUMMARY;
        SetTextColor(dc, muted ? P.muted : P.text);
        SetBkColor(dc, bgColor);
        SetBkMode(dc, TRANSPARENT);
        return (INT_PTR)bg;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = (HDC)wp;
        SetTextColor(dc, P.text);
        SetBkColor(dc, P.field);
        return (INT_PTR)g_brField;
    }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Finestra Formati
// ---------------------------------------------------------------------------
struct FmtRow {
    std::string id;
    std::wstring cols[8];
    double height = 0, rate = 0;
    bool muxed = false;
};
struct SubRow {
    std::string lang;
    bool autoCap = false;
    std::wstring name, exts;
};

struct PickResult {
    std::wstring args, label;
    int kind = 0;
    bool subs = false;
    int container = 0;
};

struct PickerCtx {
    std::wstring url, title, info;
    std::vector<FmtRow> vids, auds;
    std::vector<SubRow> subs;
    bool hasFfmpeg = false;
    bool init = true;
    PickResult out;
    HWND title_, info_, lblV, lblA, lblS, lv, la, ls, embed, srt, cont, lblCont, summary, ok, cancel;
};

static std::wstring JNumStr(double v, const wchar_t* fmt) {
    if (v <= 0) return L"";
    wchar_t b[64];
    swprintf(b, 64, fmt, v);
    return b;
}

static std::wstring ShortCodec(const std::string& c) {
    if (c.empty() || c == "none") return L"";
    std::wstring w = U(c);
    size_t dot = w.find(L'.');
    return dot == std::wstring::npos ? w : w.substr(0, dot);
}

static void BuildPickerRows(const JVal& root, PickerCtx& c) {
    c.title = U(root.str("title"));
    std::wstring dur = FormatDuration(root.num("duration"));
    std::wstring site = U(root.str("extractor_key", root.str("extractor")));
    c.info = site;
    if (!dur.empty()) c.info += (c.info.empty() ? L"" : L"   ·   ") + std::wstring(TR(L"durata ")) + dur;

    if (const JVal* fmts = root.get("formats"); fmts && fmts->t == JVal::Arr) {
        for (const JVal& f : fmts->items) {
            std::string id = f.str("format_id");
            std::string ext = f.str("ext");
            if (id.empty() || ext == "mhtml" || id.find("storyboard") != std::string::npos) continue;
            std::string vc = f.str("vcodec"), ac = f.str("acodec");
            double h = f.num("height"), wdt = f.num("width");
            bool noV = vc == "none", noA = ac == "none";
            if (noV && noA) continue;
            bool isVideo, muxed = false;
            if (noV) isVideo = false;
            else if (noA) isVideo = true;
            else if (!vc.empty() || h > 0 || wdt > 0) { isVideo = true; muxed = true; }
            else isVideo = f.str("resolution") != "audio only";
            if (isVideo && f.str("resolution") == "audio only") isVideo = false;

            double size = f.num("filesize");
            std::wstring sizeS = size > 0 ? FormatBytes(size) : (f.num("filesize_approx") > 0 ? L"~" + FormatBytes(f.num("filesize_approx")) : L"");
            std::wstring proto = U(f.str("protocol"));
            if (proto.find(L"m3u8") != std::wstring::npos) proto = L"HLS";
            else if (proto.find(L"dash") != std::wstring::npos) proto = L"DASH";
            else if (proto == L"https" || proto == L"http") proto = TR(L"diretto");
            std::wstring note = U(f.str("format_note"));
            FmtRow r;
            r.id = id;
            r.cols[0] = U(id);
            if (isVideo) {
                std::wstring res = (wdt > 0 && h > 0) ? std::to_wstring((int)wdt) + L"×" + std::to_wstring((int)h)
                                                     : U(f.str("resolution", h > 0 ? std::to_string((int)h) + "p" : ""));
                r.cols[1] = res;
                r.cols[2] = JNumStr(f.num("fps"), L"%.0f");
                r.cols[3] = ShortCodec(vc) + (muxed && !ac.empty() ? L" + " + ShortCodec(ac) : L"");
                r.cols[4] = JNumStr(f.num("tbr"), L"%.0f kbps");
                r.cols[5] = sizeS;
                r.cols[6] = proto;
                r.cols[7] = note + (muxed ? (note.empty() ? L"" : L", ") + std::wstring(TR(L"con audio")) : L"");
                r.height = h;
                r.rate = f.num("tbr");
                r.muxed = muxed;
                c.vids.push_back(std::move(r));
            } else {
                std::string lang = f.str("language");
                r.cols[1] = lang.empty() ? L"–" : U(lang);
                r.cols[2] = ShortCodec(ac);
                double abr = f.num("abr") > 0 ? f.num("abr") : f.num("tbr");
                r.cols[3] = JNumStr(abr, L"%.0f kbps");
                r.cols[4] = sizeS;
                r.cols[5] = proto;
                r.cols[6] = note;
                r.rate = abr;
                c.auds.push_back(std::move(r));
            }
        }
    }
    std::stable_sort(c.vids.begin(), c.vids.end(), [](const FmtRow& a, const FmtRow& b) {
        return a.height != b.height ? a.height > b.height : a.rate > b.rate;
    });
    std::stable_sort(c.auds.begin(), c.auds.end(), [](const FmtRow& a, const FmtRow& b) { return a.rate > b.rate; });

    auto addSubs = [&](const char* key, bool autoCap) {
        const JVal* s = root.get(key);
        if (!s || s->t != JVal::Obj) return;
        for (size_t i = 0; i < s->keys.size(); ++i) {
            if (s->keys[i] == "live_chat") continue;
            SubRow r;
            r.lang = s->keys[i];
            r.autoCap = autoCap;
            const JVal& arr = s->items[i];
            if (arr.t == JVal::Arr) {
                for (const JVal& e : arr.items) {
                    std::wstring x = U(e.str("ext"));
                    if (!x.empty() && r.exts.find(x) == std::wstring::npos) r.exts += (r.exts.empty() ? L"" : L", ") + x;
                    if (r.name.empty()) r.name = U(e.str("name"));
                }
            }
            c.subs.push_back(std::move(r));
        }
    };
    addSubs("subtitles", false);
    addSubs("automatic_captions", true);
}

static void LvColumns(HWND lv, std::initializer_list<std::pair<const wchar_t*, int>> cols) {
    int i = 0;
    for (auto& c : cols) {
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.cx = S(c.second);
        col.pszText = const_cast<LPWSTR>(c.first);
        SendMessageW(lv, LVM_INSERTCOLUMNW, i++, (LPARAM)&col);
    }
}

static void LvRow(HWND lv, int row, LPARAM param, const std::wstring* cols, int n) {
    LVITEMW li{};
    li.mask = LVIF_TEXT | LVIF_PARAM;
    li.iItem = row;
    li.pszText = const_cast<LPWSTR>(cols[0].c_str());
    li.lParam = param;
    int at = (int)SendMessageW(lv, LVM_INSERTITEMW, 0, (LPARAM)&li);
    for (int c = 1; c < n; ++c) {
        LVITEMW si{};
        si.iSubItem = c;
        si.pszText = const_cast<LPWSTR>(cols[c].c_str());
        SendMessageW(lv, LVM_SETITEMTEXTW, at, (LPARAM)&si);
    }
}

static bool LvChecked(HWND lv, int row) {
    UINT st = (UINT)SendMessageW(lv, LVM_GETITEMSTATE, row, LVIS_STATEIMAGEMASK);
    return ((st & LVIS_STATEIMAGEMASK) >> 12) == 2;
}
static void LvSetCheck(HWND lv, int row, bool on) {
    LVITEMW li{};
    li.stateMask = LVIS_STATEIMAGEMASK;
    li.state = INDEXTOSTATEIMAGEMASK(on ? 2 : 1);
    SendMessageW(lv, LVM_SETITEMSTATE, row, (LPARAM)&li);
}
static LPARAM LvParam(HWND lv, int row) {
    LVITEMW li{};
    li.mask = LVIF_PARAM;
    li.iItem = row;
    SendMessageW(lv, LVM_GETITEMW, 0, (LPARAM)&li);
    return li.lParam;
}

static std::wstring RegexEscape(const std::string& s) {
    std::wstring r;
    for (wchar_t ch : U(s)) {
        if (wcschr(L".+*?()[]{}^$|\\", ch)) r += L'\\';
        r += ch;
    }
    return r;
}

// Calcola argomenti e riepilogo dalla selezione corrente
static void PickerCompute(PickerCtx& c) {
    const FmtRow* v = nullptr;
    int sel = (int)SendMessageW(c.lv, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
    if (sel >= 0) {
        LPARAM p = LvParam(c.lv, sel);
        if (p >= 0 && p < (LPARAM)c.vids.size()) v = &c.vids[(size_t)p];
    }
    std::vector<const FmtRow*> as;
    int na = (int)SendMessageW(c.la, LVM_GETITEMCOUNT, 0, 0);
    for (int i = 0; i < na; ++i)
        if (LvChecked(c.la, i)) as.push_back(&c.auds[(size_t)LvParam(c.la, i)]);
    std::vector<const SubRow*> ss;
    int ns = (int)SendMessageW(c.ls, LVM_GETITEMCOUNT, 0, 0);
    for (int i = 0; i < ns; ++i)
        if (LvChecked(c.ls, i)) ss.push_back(&c.subs[(size_t)LvParam(c.ls, i)]);

    bool embedOn = GetWindowLongPtrW(c.embed, GWLP_USERDATA) != 0;
    bool srtOn = GetWindowLongPtrW(c.srt, GWLP_USERDATA) != 0;
    int cont = (int)GetWindowLongPtrW(c.cont, GWLP_USERDATA);

    std::wstring spec;
    if (v) spec = U(v->id);
    for (auto* a : as) spec += (spec.empty() ? L"" : L"+") + U(a->id);

    PickResult r;
    r.container = cont;
    std::wstring args;
    if (!spec.empty()) args = L"-f " + Quote(spec);
    if (as.size() > 1) args += L" --audio-multistreams";
    if (v) args += L" --merge-output-format " + ContainerExt(cont) + L" --remux-video " + ContainerExt(cont);
    else if (as.size() > 1) args += L" --merge-output-format mkv";
    if (spec.empty()) args += L" --skip-download";
    if (!ss.empty()) {
        std::wstring langs;
        bool anyManual = false, anyAuto = false;
        for (auto* s : ss) {
            langs += (langs.empty() ? L"" : L",") + RegexEscape(s->lang);
            (s->autoCap ? anyAuto : anyManual) = true;
        }
        args += L" --sub-langs " + Quote(langs);
        if (embedOn && v) args += L" --embed-subs";
        else if (anyManual) args += L" --write-subs";
        if (anyAuto) args += L" --write-auto-subs";
        if (srtOn && c.hasFfmpeg) args += L" --convert-subs srt";
        r.subs = true;
    }
    r.args = Trim(args);
    r.kind = v ? 0 : (as.empty() ? 2 : 1);

    // riepilogo leggibile
    std::wstring sum;
    if (v) sum = v->cols[1].empty() ? U(v->id) : v->cols[1];
    if (!as.empty()) {
        std::wstring a = as.size() == 1 ? (TR(L"audio ") + (as[0]->cols[1] == L"–" ? as[0]->cols[2] : as[0]->cols[1]))
                                        : std::to_wstring(as.size()) + TR(L" tracce audio");
        sum += (sum.empty() ? L"" : L" + ") + a;
    } else if (v && !v->muxed) {
        sum += TR(L" (senza audio)");
    }
    if (!ss.empty()) {
        std::wstring l;
        for (auto* s : ss) l += (l.empty() ? L"" : L",") + U(s->lang);
        sum += (sum.empty() ? TR(L"Solo sottotitoli ") : TR(L" + sub ")) + l;
    }
    if (v) sum += L" · " + std::wstring(cont ? L"MKV" : L"MP4");
    r.label = sum.empty() ? TR(L"Niente selezionato") : sum;
    c.out = r;

    SetWindowTextW(c.summary, (TR(L"Verrà scaricato: ") + r.label).c_str());
    bool any = v || !as.empty() || !ss.empty();
    EnableWindow(c.ok, any);
    EnableWindow(c.embed, v && !ss.empty());
    EnableWindow(c.srt, !ss.empty() && c.hasFfmpeg);
    EnableWindow(c.cont, v != nullptr);
    SetWindowTextW(c.cont, kContainers[cont].value());
    InvalidateRect(c.ok, nullptr, FALSE);
}

static void PickerLayout(HWND dlg, PickerCtx& c) {
    RECT rc;
    GetClientRect(dlg, &rc);
    const int m = S(18), W = rc.right, H = rc.bottom;
    int y = m;
    HDWP d = BeginDeferWindowPos(16);
    auto mv = [&](HWND w, int x, int yy, int ww, int hh) {
        d = DeferWindowPos(d, w, nullptr, x, yy, std::max(ww, 0), std::max(hh, 0), SWP_NOZORDER | SWP_NOACTIVATE);
    };
    mv(c.title_, m, y, W - 2 * m, S(26)); y += S(28);
    mv(c.info_, m, y, W - 2 * m, S(20)); y += S(28);
    const int btnH = S(36), rowH = S(32), lbl = S(22);
    int bottom = H - m - btnH - S(14) - rowH - S(14);
    int avail = std::max(bottom - y - 3 * lbl, S(150));
    int hv = avail * 42 / 100, ha = avail * 30 / 100, hs = avail - hv - ha;
    mv(c.lblV, m, y, W - 2 * m, lbl); y += lbl;
    mv(c.lv, m, y, W - 2 * m, hv - S(8)); y += hv;
    mv(c.lblA, m, y, W - 2 * m, lbl); y += lbl;
    mv(c.la, m, y, W - 2 * m, ha - S(8)); y += ha;
    mv(c.lblS, m, y, W - 2 * m, lbl); y += lbl;
    mv(c.ls, m, y, W - 2 * m, hs - S(8));
    int ry = H - m - btnH - S(14) - rowH;
    int x = m;
    int we = TextWidth(g_fUi, GetText(c.embed)) + S(34);
    mv(c.embed, x, ry, we, rowH); x += we + S(8);
    int wsr = TextWidth(g_fUi, GetText(c.srt)) + S(34);
    mv(c.srt, x, ry, wsr, rowH); x += wsr + S(28);
    int lc = TextWidth(g_fUi, TR(L"Formato file")) + S(10);
    mv(c.lblCont, x, ry, lc, rowH); x += lc;
    mv(c.cont, x, ry, S(96), rowH);
    int by = H - m - btnH;
    mv(c.ok, W - m - S(140), by, S(140), btnH);
    mv(c.cancel, W - m - S(140) - S(10) - S(110), by, S(110), btnH);
    mv(c.summary, m, by, W - 2 * m - S(270), btnH);
    EndDeferWindowPos(d);
}

static INT_PTR CALLBACK PickerProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* c = reinterpret_cast<PickerCtx*>(GetWindowLongPtrW(dlg, DWLP_USER));
    if (INT_PTR r = DialogColors(msg, wp, lp, g_brBg, P.bg)) return r;
    if (FrameMessages(dlg, msg, wp, lp)) return TRUE;
    switch (msg) {
    case WM_INITDIALOG: {
        c = reinterpret_cast<PickerCtx*>(lp);
        SetWindowLongPtrW(dlg, DWLP_USER, (LONG_PTR)c);
        SetWindowTextW(dlg, TR(L"Scegli i formati"));
        SendMessageW(dlg, WM_SETICON, ICON_BIG, (LPARAM)g_appIcon);
        auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id, DWORD ex = 0) {
            HWND h = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, dlg,
                                     (HMENU)(INT_PTR)id, g_inst, nullptr);
            SendMessageW(h, WM_SETFONT, (WPARAM)g_fUi, FALSE);
            return h;
        };
        c->title_ = mk(WC_STATICW, c->title.c_str(), SS_ENDELLIPSIS | SS_NOPREFIX, IDC_PK_TITLE);
        SendMessageW(c->title_, WM_SETFONT, (WPARAM)g_fTitle, FALSE);
        c->info_ = mk(WC_STATICW, c->info.c_str(), SS_ENDELLIPSIS | SS_NOPREFIX, IDC_PK_INFO);
        c->lblV = mk(WC_STATICW, (TR(L"VIDEO   (") + std::to_wstring(c->vids.size()) + L")").c_str(), 0, IDC_PK_LBLV);
        c->lblA = mk(WC_STATICW, (L"AUDIO   (" + std::to_wstring(c->auds.size()) + TR(L")   –   puoi spuntarne più di una")).c_str(), 0, IDC_PK_LBLA);
        c->lblS = mk(WC_STATICW, (TR(L"SOTTOTITOLI   (") + std::to_wstring(c->subs.size()) + TR(L")   –   anche da soli, senza video né audio")).c_str(), 0, IDC_PK_LBLS);
        for (HWND h : { c->lblV, c->lblA, c->lblS }) SendMessageW(h, WM_SETFONT, (WPARAM)g_fSmallBold, FALSE);
        DWORD lvs = WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER;
        c->lv = mk(WC_LISTVIEWW, L"", lvs | LVS_SINGLESEL, IDC_PK_VIDEO);
        c->la = mk(WC_LISTVIEWW, L"", lvs, IDC_PK_AUDIO);
        c->ls = mk(WC_LISTVIEWW, L"", lvs, IDC_PK_SUBS);
        DWORD ex = LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP;
        SendMessageW(c->lv, LVM_SETEXTENDEDLISTVIEWSTYLE, ex, ex);
        SendMessageW(c->la, LVM_SETEXTENDEDLISTVIEWSTYLE, ex | LVS_EX_CHECKBOXES, ex | LVS_EX_CHECKBOXES);
        SendMessageW(c->ls, LVM_SETEXTENDEDLISTVIEWSTYLE, ex | LVS_EX_CHECKBOXES, ex | LVS_EX_CHECKBOXES);
        UseAppCheckboxes(c->la);
        UseAppCheckboxes(c->ls);
        for (HWND l : { c->lv, c->la, c->ls }) MarkFrame(l, 2);
        LvColumns(c->lv, { { L"ID", 110 }, { TR(L"Risoluzione"), 110 }, { L"FPS", 50 }, { L"Codec", 120 }, { L"Bitrate", 90 },
                           { TR(L"Dimensione"), 90 }, { TR(L"Tipo"), 70 }, { TR(L"Note"), 160 } });
        LvColumns(c->la, { { L"ID", 140 }, { TR(L"Lingua"), 90 }, { L"Codec", 90 }, { L"Bitrate", 90 }, { TR(L"Dimensione"), 90 },
                           { TR(L"Tipo"), 70 }, { TR(L"Note"), 200 } });
        LvColumns(c->ls, { { TR(L"Lingua"), 140 }, { TR(L"Nome"), 220 }, { TR(L"Formati"), 160 }, { TR(L"Tipo"), 130 } });

        std::wstring none[8] = { L"—", TR(L"Nessun video"), L"", L"", L"", L"", L"", L"" };
        LvRow(c->lv, 0, -1, none, 8);
        for (size_t i = 0; i < c->vids.size(); ++i) LvRow(c->lv, (int)i + 1, (LPARAM)i, c->vids[i].cols, 8);
        for (size_t i = 0; i < c->auds.size(); ++i) LvRow(c->la, (int)i, (LPARAM)i, c->auds[i].cols, 7);
        for (size_t i = 0; i < c->subs.size(); ++i) {
            std::wstring cols[4] = { U(c->subs[i].lang), c->subs[i].name, c->subs[i].exts,
                                     c->subs[i].autoCap ? TR(L"automatici") : TR(L"originali") };
            LvRow(c->ls, (int)i, (LPARAM)i, cols, 4);
        }

        c->embed = mk(WC_BUTTONW, TR(L"Sottotitoli dentro il video"), WS_TABSTOP | BS_OWNERDRAW, IDC_PK_EMBED);
        c->srt = mk(WC_BUTTONW, TR(L"Converti in SRT"), WS_TABSTOP | BS_OWNERDRAW, IDC_PK_SRT);
        c->lblCont = mk(WC_STATICW, TR(L"Formato file"), SS_CENTERIMAGE, IDC_PK_INFO + 100);
        c->cont = mk(WC_BUTTONW, L"MP4", WS_TABSTOP | BS_OWNERDRAW, IDC_PK_CONT);
        c->summary = mk(WC_STATICW, L"", SS_ENDELLIPSIS | SS_CENTERIMAGE | SS_NOPREFIX, IDC_PK_SUMMARY);
        c->cancel = mk(WC_BUTTONW, TR(L"Annulla"), WS_TABSTOP | BS_OWNERDRAW, IDCANCEL);
        c->ok = mk(WC_BUTTONW, TR(L"Scarica"), WS_TABSTOP | BS_OWNERDRAW, IDOK);
        SetWindowLongPtrW(c->embed, GWLP_USERDATA, 1);
        SetWindowLongPtrW(c->srt, GWLP_USERDATA, 1);
        SetWindowLongPtrW(c->cont, GWLP_USERDATA, g_set.preset.container);
        for (HWND b : { c->embed, c->srt, c->cont, c->cancel, c->ok })
            SetWindowSubclass(b, BtnSubclass, 1, 0);

        // Selezione iniziale: miglior video + miglior traccia audio (se il video non ha gia' l'audio)
        int vsel = c->vids.empty() ? 0 : 1;
        LVITEMW li{};
        li.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
        li.state = LVIS_SELECTED | LVIS_FOCUSED;
        SendMessageW(c->lv, LVM_SETITEMSTATE, vsel, (LPARAM)&li);
        bool vMuxed = !c->vids.empty() && c->vids[0].muxed;
        if (!c->auds.empty() && !vMuxed) LvSetCheck(c->la, 0, true);

        ThemeDialog(dlg);
        RECT pr;
        GetWindowRect(g_hwnd, &pr);
        int ww = std::min(S(900), GetSystemMetrics(SM_CXSCREEN) - S(40));
        int wh = std::min(S(680), GetSystemMetrics(SM_CYSCREEN) - S(80));
        SetWindowPos(dlg, nullptr, (pr.left + pr.right - ww) / 2, std::max<LONG>(0, (pr.top + pr.bottom - wh) / 2), ww, wh,
                     SWP_NOZORDER);
        PickerLayout(dlg, *c);
        c->init = false;
        PickerCompute(*c);
        SetFocus(c->lv);
        return FALSE;
    }
    case WM_SIZE:
        if (c) { PickerLayout(dlg, *c); InvalidateRect(dlg, nullptr, TRUE); }
        return TRUE;
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize = { S(720), S(520) };
        return TRUE;
    case WM_DRAWITEM:
        DrawButton(reinterpret_cast<DRAWITEMSTRUCT*>(lp));
        return TRUE;
    case WM_NOTIFY: {
        auto* nh = reinterpret_cast<NMHDR*>(lp);
        if (!c || c->init) break;
        if (nh->code == LVN_ITEMCHANGED) {
            auto* nl = reinterpret_cast<NMLISTVIEW*>(lp);
            if (nl->uChanged & LVIF_STATE) PickerCompute(*c);
        } else if (nh->code == NM_CLICK && (nh->hwndFrom == c->la || nh->hwndFrom == c->ls)) {
            // clic sulla riga (non solo sulla casella) = spunta
            auto* ia = reinterpret_cast<NMITEMACTIVATE*>(lp);
            LVHITTESTINFO ht{};
            ht.pt = ia->ptAction;
            int row = (int)SendMessageW(nh->hwndFrom, LVM_SUBITEMHITTEST, 0, (LPARAM)&ht);
            if (row >= 0 && !(ht.flags & LVHT_ONITEMSTATEICON)) LvSetCheck(nh->hwndFrom, row, !LvChecked(nh->hwndFrom, row));
        } else if (nh->code == NM_DBLCLK && nh->hwndFrom == c->lv && IsWindowEnabled(c->ok)) {
            SendMessageW(dlg, WM_COMMAND, IDOK, 0);
        }
        break;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_PK_EMBED:
        case IDC_PK_SRT: {
            HWND b = GetDlgItem(dlg, LOWORD(wp));
            SetWindowLongPtrW(b, GWLP_USERDATA, !GetWindowLongPtrW(b, GWLP_USERDATA));
            InvalidateRect(b, nullptr, FALSE);
            PickerCompute(*c);
            return TRUE;
        }
        case IDC_PK_CONT: {
            HMENU m = CreatePopupMenu();
            for (int i = 0; i < CountOf(kContainers); ++i)
                AppendMenuW(m, MF_STRING, ID_PKCONT_BASE + i,
                            (std::wstring(kContainers[i].label()) + L"\t" + kContainers[i].hint()).c_str());
            int cur = (int)GetWindowLongPtrW(c->cont, GWLP_USERDATA);
            CheckMenuRadioItem(m, ID_PKCONT_BASE, ID_PKCONT_BASE + CountOf(kContainers) - 1, ID_PKCONT_BASE + cur, MF_BYCOMMAND);
            RECT r;
            GetWindowRect(c->cont, &r);
            int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_BOTTOMALIGN, r.left, r.top - S(4), 0, dlg, nullptr);
            DestroyMenu(m);
            if (cmd >= ID_PKCONT_BASE && cmd < ID_PKCONT_BASE + CountOf(kContainers)) {
                SetWindowLongPtrW(c->cont, GWLP_USERDATA, cmd - ID_PKCONT_BASE);
                PickerCompute(*c);
                InvalidateRect(c->cont, nullptr, FALSE);
            }
            return TRUE;
        }
        case IDOK:
            if (!c) return TRUE;
            PickerCompute(*c);
            if (c->out.args.empty() || !IsWindowEnabled(c->ok)) return TRUE;
            EndDialog(dlg, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

// ---------------------------------------------------------------------------
// Finestra "Scegli la risoluzione" (qualita' = Chiedi ogni volta)
// ---------------------------------------------------------------------------
struct QuickRow {
    int height = 0;          // -2 = solo audio
    std::wstring cols[3];
};

struct QuickCtx {
    std::wstring title, info;
    std::vector<QuickRow> rows;
    int container = 0;
    // risultato
    int height = 0;
    bool audioOnly = false;
    bool advanced = false;
    HWND title_, info_, lbl, lv, lblCont, cont, adv, ok, cancel;
};

static std::wstring HeightName(int h) {
    switch (h) {
    case 4320: return L"4320p (8K)";
    case 2160: return L"2160p (4K)";
    case 1440: return L"1440p (2K)";
    case 1080: return L"1080p (Full HD)";
    case 720:  return L"720p (HD)";
    default:   return std::to_wstring(h) + L"p";
    }
}

static double FmtSize(const JVal& f) {
    double s = f.num("filesize");
    if (s <= 0) s = f.num("filesize_approx");
    if (s <= 0 && f.num("tbr") > 0 && f.num("duration") > 0) s = f.num("tbr") * 1000 / 8 * f.num("duration");
    return s;
}

static void BuildQuickRows(const JVal& root, QuickCtx& c) {
    c.title = U(root.str("title"));
    std::wstring dur = FormatDuration(root.num("duration"));
    c.info = U(root.str("extractor_key", root.str("extractor")));
    if (!dur.empty()) c.info += (c.info.empty() ? L"" : L"   ·   ") + std::wstring(TR(L"durata ")) + dur;
    const double duration = root.num("duration");

    struct Best { double tbr = -1, size = 0, fps = 0; std::string vc; bool muxed = false; };
    std::vector<std::pair<int, Best>> byH;
    double bestAudio = 0, bestAudioRate = -1;
    if (const JVal* fmts = root.get("formats"); fmts && fmts->t == JVal::Arr) {
        for (const JVal& f : fmts->items) {
            std::string vc = f.str("vcodec"), ac = f.str("acodec");
            if (f.str("ext") == "mhtml") continue;
            int h = (int)f.num("height");
            double size = FmtSize(f);
            if (size <= 0 && f.num("tbr") > 0 && duration > 0) size = f.num("tbr") * 1000 / 8 * duration;
            if (vc == "none" || (h <= 0 && f.str("resolution") == "audio only")) {
                double rate = f.num("abr") > 0 ? f.num("abr") : f.num("tbr");
                if (ac != "none" && rate > bestAudioRate) { bestAudioRate = rate; bestAudio = size; }
                continue;
            }
            if (h <= 0) continue;
            auto it = std::find_if(byH.begin(), byH.end(), [h](const std::pair<int, Best>& p) { return p.first == h; });
            if (it == byH.end()) { byH.push_back({ h, Best{} }); it = byH.end() - 1; }
            double tbr = f.num("tbr");
            if (tbr > it->second.tbr) {
                it->second.tbr = tbr;
                it->second.size = size;
                it->second.fps = f.num("fps");
                it->second.vc = vc;
                it->second.muxed = !ac.empty() && ac != "none";
            }
        }
    }
    std::sort(byH.begin(), byH.end(), [](const std::pair<int, Best>& a, const std::pair<int, Best>& b) { return a.first > b.first; });
    for (auto& [h, b] : byH) {
        QuickRow r;
        r.height = h;
        r.cols[0] = HeightName(h) + (b.fps > 30 ? L"  " + std::to_wstring((int)(b.fps + 0.5)) + L" fps" : L"");
        double total = b.size + (b.muxed ? 0 : bestAudio);
        r.cols[1] = total > 0 ? L"~" + FormatBytes(total) : L"–";
        r.cols[2] = ShortCodec(b.vc) + (b.tbr > 0 ? L"   " + JNumStr(b.tbr, L"%.0f kbps") : L"");
        c.rows.push_back(std::move(r));
    }
    if (bestAudioRate >= 0 || !byH.empty()) {
        QuickRow r;
        r.height = -2;
        r.cols[0] = TR(L"Solo audio");
        r.cols[1] = bestAudio > 0 ? L"~" + FormatBytes(bestAudio) : L"–";
        r.cols[2] = std::wstring(TR(L"come impostato: ")) + kAudioFmts[g_set.preset.afmt].value();
        c.rows.push_back(std::move(r));
    }
}

static void QuickLayout(HWND dlg, QuickCtx& c) {
    RECT rc;
    GetClientRect(dlg, &rc);
    const int m = S(18), W = rc.right, H = rc.bottom, btnH = S(34);
    int y = m;
    HDWP d = BeginDeferWindowPos(10);
    auto mv = [&](HWND w, int x, int yy, int ww, int hh) {
        d = DeferWindowPos(d, w, nullptr, x, yy, std::max(ww, 0), std::max(hh, 0), SWP_NOZORDER | SWP_NOACTIVATE);
    };
    mv(c.title_, m, y, W - 2 * m, S(26)); y += S(28);
    mv(c.info_, m, y, W - 2 * m, S(20)); y += S(30);
    mv(c.lbl, m, y, W - 2 * m, S(20)); y += S(24);
    int by = H - m - btnH;
    mv(c.lv, m, y, W - 2 * m, by - S(14) - y);
    int lc = TextWidth(g_fUi, TR(L"Formato file")) + S(10);
    mv(c.lblCont, m, by, lc, btnH);
    mv(c.cont, m + lc, by, S(90), btnH);
    mv(c.ok, W - m - S(120), by, S(120), btnH);
    mv(c.cancel, W - m - S(120) - S(8) - S(96), by, S(96), btnH);
    mv(c.adv, W - m - S(120) - S(8) - S(96) - S(8) - S(110), by, S(110), btnH);
    EndDeferWindowPos(d);
}

static INT_PTR CALLBACK QuickProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* c = reinterpret_cast<QuickCtx*>(GetWindowLongPtrW(dlg, DWLP_USER));
    if (INT_PTR r = DialogColors(msg, wp, lp, g_brBg, P.bg)) return r;
    if (FrameMessages(dlg, msg, wp, lp)) return TRUE;
    switch (msg) {
    case WM_INITDIALOG: {
        c = reinterpret_cast<QuickCtx*>(lp);
        SetWindowLongPtrW(dlg, DWLP_USER, (LONG_PTR)c);
        SetWindowTextW(dlg, TR(L"Scegli la risoluzione"));
        SendMessageW(dlg, WM_SETICON, ICON_BIG, (LPARAM)g_appIcon);
        auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id, DWORD ex = 0) {
            HWND h = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, dlg,
                                     (HMENU)(INT_PTR)id, g_inst, nullptr);
            SendMessageW(h, WM_SETFONT, (WPARAM)g_fUi, FALSE);
            return h;
        };
        c->title_ = mk(WC_STATICW, c->title.c_str(), SS_ENDELLIPSIS | SS_NOPREFIX, IDC_PK_TITLE);
        SendMessageW(c->title_, WM_SETFONT, (WPARAM)g_fTitle, FALSE);
        c->info_ = mk(WC_STATICW, c->info.c_str(), SS_ENDELLIPSIS | SS_NOPREFIX, IDC_PK_INFO);
        c->lbl = mk(WC_STATICW, TR(L"RISOLUZIONI DISPONIBILI PER QUESTO VIDEO"), 0, IDC_PK_LBLV);
        SendMessageW(c->lbl, WM_SETFONT, (WPARAM)g_fSmallBold, FALSE);
        c->lv = mk(WC_LISTVIEWW, L"", WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER, IDC_PK_LIST);
        DWORD ex = LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER;
        SendMessageW(c->lv, LVM_SETEXTENDEDLISTVIEWSTYLE, ex, ex);
        LvColumns(c->lv, { { TR(L"Risoluzione"), 190 }, { TR(L"Dimensione stimata"), 140 }, { TR(L"Dettagli"), 200 } });
        MarkFrame(c->lv, 2);
        for (size_t i = 0; i < c->rows.size(); ++i) LvRow(c->lv, (int)i, (LPARAM)i, c->rows[i].cols, 3);
        c->lblCont = mk(WC_STATICW, TR(L"Formato file"), SS_CENTERIMAGE, IDC_PK_INFO + 101);
        c->cont = mk(WC_BUTTONW, kContainers[c->container].value(), WS_TABSTOP | BS_OWNERDRAW, IDC_PK_CONT);
        SetWindowLongPtrW(c->cont, GWLP_USERDATA, c->container);
        c->adv = mk(WC_BUTTONW, TR(L"Avanzate..."), WS_TABSTOP | BS_OWNERDRAW, IDC_PK_ADV);
        c->cancel = mk(WC_BUTTONW, TR(L"Annulla"), WS_TABSTOP | BS_OWNERDRAW, IDCANCEL);
        c->ok = mk(WC_BUTTONW, TR(L"Scarica"), WS_TABSTOP | BS_OWNERDRAW, IDOK);
        for (HWND b : { c->cont, c->adv, c->cancel, c->ok }) SetWindowSubclass(b, BtnSubclass, 1, 0);

        // preselezione: la risoluzione piu' vicina (per difetto) a 1080p, altrimenti la prima
        int sel = 0;
        for (size_t i = 0; i < c->rows.size(); ++i)
            if (c->rows[i].height > 0 && c->rows[i].height <= 1080) { sel = (int)i; break; }
        LVITEMW li{};
        li.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
        li.state = LVIS_SELECTED | LVIS_FOCUSED;
        SendMessageW(c->lv, LVM_SETITEMSTATE, sel, (LPARAM)&li);
        SendMessageW(c->lv, LVM_ENSUREVISIBLE, sel, FALSE);

        ThemeDialog(dlg);
        RECT pr;
        GetWindowRect(g_hwnd, &pr);
        int ww = std::min(S(620), GetSystemMetrics(SM_CXSCREEN) - S(40));
        int wh = std::min(S(470), GetSystemMetrics(SM_CYSCREEN) - S(80));
        SetWindowPos(dlg, nullptr, (pr.left + pr.right - ww) / 2, std::max<LONG>(0, (pr.top + pr.bottom - wh) / 2), ww, wh, SWP_NOZORDER);
        QuickLayout(dlg, *c);
        SetFocus(c->lv);
        return FALSE;
    }
    case WM_SIZE:
        if (c) { QuickLayout(dlg, *c); InvalidateRect(dlg, nullptr, TRUE); }
        return TRUE;
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize = { S(560), S(380) };
        return TRUE;
    case WM_DRAWITEM:
        DrawButton(reinterpret_cast<DRAWITEMSTRUCT*>(lp));
        return TRUE;
    case WM_NOTIFY: {
        auto* nh = reinterpret_cast<NMHDR*>(lp);
        if (c && nh->hwndFrom == c->lv && nh->code == NM_DBLCLK) SendMessageW(dlg, WM_COMMAND, IDOK, 0);
        break;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_PK_CONT: {
            HMENU m = CreatePopupMenu();
            for (int i = 0; i < CountOf(kContainers); ++i)
                AppendMenuW(m, MF_STRING, ID_PKCONT_BASE + i,
                            (std::wstring(kContainers[i].label()) + L"\t" + kContainers[i].hint()).c_str());
            CheckMenuRadioItem(m, ID_PKCONT_BASE, ID_PKCONT_BASE + CountOf(kContainers) - 1, ID_PKCONT_BASE + c->container, MF_BYCOMMAND);
            RECT r;
            GetWindowRect(c->cont, &r);
            int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_BOTTOMALIGN, r.left, r.top - S(4), 0, dlg, nullptr);
            DestroyMenu(m);
            if (cmd >= ID_PKCONT_BASE && cmd < ID_PKCONT_BASE + CountOf(kContainers)) {
                c->container = cmd - ID_PKCONT_BASE;
                SetWindowLongPtrW(c->cont, GWLP_USERDATA, c->container);
                SetWindowTextW(c->cont, kContainers[c->container].value());
            }
            return TRUE;
        }
        case IDOK: {
            int sel = (int)SendMessageW(c->lv, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
            if (sel < 0 || sel >= (int)c->rows.size()) return TRUE;
            c->height = c->rows[(size_t)sel].height;
            c->audioOnly = c->height == -2;
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        case IDC_PK_ADV:
            c->advanced = true;
            EndDialog(dlg, IDC_PK_ADV);
            return TRUE;
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

// ---------------------------------------------------------------------------
// Impostazioni
// ---------------------------------------------------------------------------
static INT_PTR CALLBACK SettingsProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    if (INT_PTR r = DialogColors(msg, wp, lp, g_brBg, P.bg)) return r;
    if (FrameMessages(dlg, msg, wp, lp)) return TRUE;
    switch (msg) {
    case WM_DRAWITEM:
        DrawButton(reinterpret_cast<DRAWITEMSTRUCT*>(lp));
        return TRUE;
    case WM_INITDIALOG:
        SendMessageW(dlg, WM_SETICON, ICON_BIG, (LPARAM)g_appIcon);
        TranslateWindow(dlg);   // testi della risorsa (in italiano)
        SetDlgItemTextW(dlg, IDC_SET_YTDLP, g_set.ytdlp.c_str());
        SetDlgItemTextW(dlg, IDC_SET_FFMPEG, g_set.ffmpeg.c_str());
        SetDlgItemTextW(dlg, IDC_SET_SUBLANGS, g_set.subLangs.c_str());
        SetDlgItemTextW(dlg, IDC_SET_EXTRA, g_set.extra.c_str());
        SendDlgItemMessageW(dlg, IDC_SET_MAXDL_SPIN, UDM_SETRANGE32, 1, 8);
        SendDlgItemMessageW(dlg, IDC_SET_MAXDL_SPIN, UDM_SETPOS32, 0, g_set.maxDl);
        SendDlgItemMessageW(dlg, IDC_SET_FRAGS_SPIN, UDM_SETRANGE32, 1, 32);
        SendDlgItemMessageW(dlg, IDC_SET_FRAGS_SPIN, UDM_SETPOS32, 0, g_set.conns);
        CheckDlgButton(dlg, IDC_SET_AUTOSUBS, g_set.autoSubs ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, IDC_SET_TRAY, g_set.tray ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, IDC_SET_SEGMENTED, g_set.segmented ? BST_CHECKED : BST_UNCHECKED);
        ThemeDialog(dlg);
        // stile dell'app: caselle di spunta, pulsanti e cornici dei campi
        for (int id : { IDC_SET_SEGMENTED, IDC_SET_AUTOSUBS, IDC_SET_TRAY }) {
            HWND h = GetDlgItem(dlg, id);
            OwnerDrawButton(h, true);
            RECT r;
            GetWindowRect(h, &r);
            MapWindowPoints(nullptr, dlg, (POINT*)&r, 2);
            int cy = (r.top + r.bottom) / 2, hh = S(22);
            SetWindowPos(h, nullptr, r.left, cy - hh / 2, r.right - r.left, hh, SWP_NOZORDER);
        }
        for (int id : { IDC_SET_YTDLP_BR, IDC_SET_FFMPEG_BR, IDOK, IDCANCEL }) OwnerDrawButton(GetDlgItem(dlg, id), false);
        SetPropW(GetDlgItem(dlg, IDOK), L"stk.noglyph", (HANDLE)1);
        for (int id : { IDC_SET_YTDLP_BR, IDC_SET_FFMPEG_BR, IDOK, IDCANCEL }) SetPropW(GetDlgItem(dlg, id), L"stk.rect", (HANDLE)1);
        for (int id : { IDC_SET_YTDLP, IDC_SET_FFMPEG, IDC_SET_SUBLANGS, IDC_SET_EXTRA }) MarkFrame(GetDlgItem(dlg, id), 1);
        MarkFrame(GetDlgItem(dlg, IDC_SET_MAXDL), 1, GetDlgItem(dlg, IDC_SET_MAXDL_SPIN));
        MarkFrame(GetDlgItem(dlg, IDC_SET_FRAGS), 1, GetDlgItem(dlg, IDC_SET_FRAGS_SPIN));
        for (int id : { IDC_SET_MAXDL_SPIN, IDC_SET_FRAGS_SPIN }) SetWindowSubclass(GetDlgItem(dlg, id), SpinSubclass, 1, 0);
        InvalidateRect(dlg, nullptr, TRUE);
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_SET_SEGMENTED:
        case IDC_SET_AUTOSUBS:
        case IDC_SET_TRAY: {
            HWND b = GetDlgItem(dlg, LOWORD(wp));
            SetWindowLongPtrW(b, GWLP_USERDATA, !GetWindowLongPtrW(b, GWLP_USERDATA));
            InvalidateRect(b, nullptr, FALSE);
            return TRUE;
        }
        case IDC_SET_YTDLP_BR:
        case IDC_SET_FFMPEG_BR: {
            int edit = LOWORD(wp) == IDC_SET_YTDLP_BR ? IDC_SET_YTDLP : IDC_SET_FFMPEG;
            IFileOpenDialog* fd = nullptr;
            if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fd)))) {
                COMDLG_FILTERSPEC spec[] = { { L"Eseguibili (*.exe)", L"*.exe" } };
                fd->SetFileTypes(1, spec);
                if (SUCCEEDED(fd->Show(dlg))) {
                    IShellItem* si = nullptr;
                    if (SUCCEEDED(fd->GetResult(&si))) {
                        PWSTR p = nullptr;
                        if (SUCCEEDED(si->GetDisplayName(SIGDN_FILESYSPATH, &p))) { SetDlgItemTextW(dlg, edit, p); CoTaskMemFree(p); }
                        si->Release();
                    }
                }
                fd->Release();
            }
            return TRUE;
        }
        case IDOK: {
            BOOL okConv = FALSE;
            g_set.ytdlp     = Trim(GetText(GetDlgItem(dlg, IDC_SET_YTDLP)));
            g_set.ffmpeg    = Trim(GetText(GetDlgItem(dlg, IDC_SET_FFMPEG)));
            g_set.subLangs  = Trim(GetText(GetDlgItem(dlg, IDC_SET_SUBLANGS)));
            g_set.extra     = Trim(GetText(GetDlgItem(dlg, IDC_SET_EXTRA)));
            g_set.maxDl     = std::clamp((int)SendDlgItemMessageW(dlg, IDC_SET_MAXDL_SPIN, UDM_GETPOS32, 0, (LPARAM)&okConv), 1, 8);
            g_set.conns     = std::clamp((int)SendDlgItemMessageW(dlg, IDC_SET_FRAGS_SPIN, UDM_GETPOS32, 0, (LPARAM)&okConv), 1, 32);
            auto on = [&](int id) { return GetWindowLongPtrW(GetDlgItem(dlg, id), GWLP_USERDATA) != 0; };
            g_set.autoSubs  = on(IDC_SET_AUTOSUBS);
            g_set.tray      = on(IDC_SET_TRAY);
            g_set.segmented = on(IDC_SET_SEGMENTED);
            SaveSettings();
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

// ---------------------------------------------------------------------------
// Informazioni su Stackly
// ---------------------------------------------------------------------------
static void SetClipboardText(const std::wstring& s);

struct AboutComp {
    const wchar_t* name;
    const wchar_t* role;
    std::wstring path, version;
    bool checked = false;
};

struct AboutVers { std::wstring v[3]; };

struct AboutCtx {
    AboutComp comp[3] = { { L"yt-dlp", N_(L"estrazione dei link"), {}, {}, false },
                          { L"FFmpeg", N_(L"unione, conversione, sottotitoli"), {}, {}, false },
                          { L"Deno", N_(L"necessario a yt-dlp per YouTube"), {}, {}, false } };
    HFONT fHero = nullptr, fSect = nullptr;
    HWND update = nullptr, tools = nullptr, copy = nullptr, close = nullptr;
    RECT link{};          // area cliccabile del link al progetto
    bool linkHot = false;
};

// Prima riga utile dell'output "--version" di un componente
static std::wstring ToolVersion(const std::wstring& exe, const wchar_t* arg, int which) {
    std::string out;
    DWORD rc = 1;
    if (!RunCaptureEx(exe, arg, out, nullptr, rc)) return L"";
    std::wstring w = BytesToW(out);
    w = Trim(w.substr(0, w.find_first_of(L"\r\n")));
    if (which == 1) {   // "ffmpeg version N-12345-gabc... Copyright ..." -> "N-12345-gabc"
        size_t p = w.find(L"version ");
        if (p != std::wstring::npos) { w = w.substr(p + 8); w = w.substr(0, w.find(L' ')); }
    } else if (which == 2) {   // "deno 2.1.4 (stable, release, ...)" -> "2.1.4"
        if (StartsWith(w, L"deno ")) { w = w.substr(5); w = w.substr(0, w.find(L' ')); }
    }
    if (w.size() > 40) w = w.substr(0, 40) + L"…";
    return w;
}

struct AboutJob { HWND dlg; std::wstring path[3]; };

static void CALLBACK AboutWork(PTP_CALLBACK_INSTANCE, void* p) {
    std::unique_ptr<AboutJob> j(static_cast<AboutJob*>(p));
    auto* r = new AboutVers;
    static const wchar_t* args[3] = { L"--version", L"-version", L"--version" };
    for (int i = 0; i < 3; ++i)
        if (!j->path[i].empty()) r->v[i] = ToolVersion(j->path[i], args[i], i);
    if (!IsWindow(j->dlg) || !PostMessageW(j->dlg, WM_APP_ABOUTVER, 0, (LPARAM)r)) delete r;
}

static std::wstring AboutText(const AboutCtx& c) {
    std::wstring s = std::wstring(kAppName) + L" " + kVersion + L"  (" + kRepoUrl + L")\r\n";
    for (const auto& k : c.comp) {
        s += std::wstring(k.name) + L": ";
        if (k.path.empty()) s += TR(L"non trovato");
        else s += (k.version.empty() ? L"" : k.version + L"  ") + L"(" + k.path + L")";
        s += L"\r\n";
    }
    s += TR(L"Impostazioni: ") + g_ini + TR(L"\r\nStrumenti: ") + BinDir() + L"\r\n";
    return s;
}

// Geometria comune a disegno e layout
struct AboutGeo { RECT hero, icon, sect, rows[3], paths, buttons; };
static AboutGeo AboutLayoutGeo(const RECT& rc) {
    AboutGeo g{};
    const int m = S(24), W = rc.right;
    g.hero = { 0, 0, W, S(150) };
    g.icon = { m, S(26), m + S(100), S(126) };
    g.sect = { m, g.hero.bottom + S(10), W - m, g.hero.bottom + S(30) };
    int y = g.sect.bottom + S(4);
    for (int i = 0; i < 3; ++i) { g.rows[i] = { m, y, W - m, y + S(52) }; y += S(58); }
    g.paths = { m, y + S(2), W - m, y + S(44) };
    g.buttons = { m, rc.bottom - m - S(36), W - m, rc.bottom - m };
    return g;
}

static void AboutPaint(HWND dlg, HDC hdc, AboutCtx& c) {
    RECT rc;
    GetClientRect(dlg, &rc);
    MemDC m(hdc, rc.right, rc.bottom);
    FillRect(m.dc, &rc, g_brBg);
    AboutGeo g = AboutLayoutGeo(rc);
    {
        Gdiplus::Graphics gr(m.dc);
        PrepG(gr);
        // fascia alta: alone del gradiente dell'icona che sfuma nello sfondo
        Gdiplus::GraphicsPath glow;
        glow.AddEllipse((float)-S(80), (float)-S(160), (float)S(420), (float)S(320));
        Gdiplus::PathGradientBrush pb(&glow);
        Gdiplus::Color center = GC(P.gradA, g_dark ? 90 : 55), edge = GC(P.bg, 0);
        pb.SetCenterColor(center);
        int n = 1;
        pb.SetSurroundColors(&edge, &n);
        gr.FillPath(&pb, &glow);
        Gdiplus::GraphicsPath glow2;
        glow2.AddEllipse((float)rc.right - S(260), (float)-S(140), (float)S(380), (float)S(280));
        Gdiplus::PathGradientBrush pb2(&glow2);
        Gdiplus::Color c2 = GC(P.gradB, g_dark ? 70 : 40);
        pb2.SetCenterColor(c2);
        pb2.SetSurroundColors(&edge, &n);
        gr.FillPath(&pb2, &glow2);
        // linea in gradiente sotto la fascia
        RECT ln{ S(24), g.hero.bottom - 1, rc.right - S(24), g.hero.bottom + 1 };
        FillRoundGrad(gr, ln, 0, P.gradA, P.gradB, 160);
        // righe dei componenti
        for (int i = 0; i < 3; ++i) {
            FillRound(gr, g.rows[i], (float)S(10), P.surface);
            if (!g_dark) StrokeRound(gr, g.rows[i], (float)S(10), Mix(P.border, P.surface, 0.3));
            int cy = (g.rows[i].top + g.rows[i].bottom) / 2;
            RECT dot{ g.rows[i].left + S(16), cy - S(5), g.rows[i].left + S(26), cy + S(5) };
            bool ok = !c.comp[i].path.empty();
            FillRound(gr, dot, (float)S(5), ok ? P.green : P.red);
            RECT halo = dot;
            InflateRect(&halo, S(4), S(4));
            FillRound(gr, halo, (float)S(9), ok ? P.green : P.red, 45);
        }
    }
    DrawIconEx(m.dc, g.icon.left, g.icon.top, g_appIcon, g.icon.right - g.icon.left, g.icon.bottom - g.icon.top, 0, nullptr,
               DI_NORMAL);
    int tx = g.icon.right + S(22);
    RECT t1{ tx, S(30), rc.right - S(24), S(72) };
    DrawText2(m.dc, kAppName, t1, c.fHero, P.text, DT_LEFT | DT_BOTTOM | DT_SINGLELINE);
    // pillola della versione
    std::wstring ver = std::wstring(TR(L"versione ")) + kVersion;
    int vw = TextWidth(g_fSmallBold, ver) + S(20);
    RECT vp{ tx, S(78), tx + vw, S(100) };
    {
        Gdiplus::Graphics gr(m.dc);
        PrepG(gr);
        FillRoundGrad(gr, vp, (float)S(11), P.gradA, P.gradB);
    }
    DrawText2(m.dc, ver, vp, g_fSmallBold, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    RECT t3{ tx, S(106), rc.right - S(24), S(128) };
    DrawText2(m.dc, TR(L"Scarica video e audio da YouTube, RaiPlay e migliaia di siti, con download segmentato nativo."),
              t3, g_fSmall, P.muted, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    DrawText2(m.dc, TR(L"COMPONENTI"), g.sect, c.fSect, P.muted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    for (int i = 0; i < 3; ++i) {
        const AboutComp& k = c.comp[i];
        RECT r = g.rows[i];
        int x = r.left + S(42);
        RECT n1{ x, r.top + S(7), x + S(90), r.top + S(27) };
        DrawText2(m.dc, k.name, n1, g_fBold, P.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        RECT rl{ x + S(76), r.top + S(7), r.right - S(170), r.top + S(27) };
        DrawText2(m.dc, TR(k.role), rl, g_fSmall, P.muted, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        std::wstring st = k.path.empty() ? TR(L"non trovato") : (k.version.empty() ? (k.checked ? TR(L"installato") : TR(L"verifica…")) : k.version);
        RECT sr{ r.right - S(170), r.top + S(7), r.right - S(14), r.top + S(27) };
        DrawText2(m.dc, st, sr, k.path.empty() ? g_fUi : g_fBold, k.path.empty() ? P.red : P.text,
                  DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        RECT pr{ x, r.top + S(27), r.right - S(14), r.bottom - S(6) };
        std::wstring ps = k.path.empty() ? TR(L"Si scarica con “Aggiorna componenti” qui sotto") : k.path;
        DrawText2(m.dc, ps, pr, g_fSmall, P.muted, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS);
    }
    RECT p1{ g.paths.left, g.paths.top, g.paths.right, g.paths.top + S(20) };
    DrawText2(m.dc, TR(L"Impostazioni:  ") + g_ini, p1, g_fSmall, P.muted, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS);
    // progetto su GitHub (link) e crediti
    RECT p2{ g.paths.left, g.paths.top + S(20), g.paths.right, g.paths.bottom };
    const wchar_t* lead = TR(L"Progetto:  ");
    int lw = TextWidth(g_fSmall, lead), kw = TextWidth(g_fSmallBold, kRepoShort);
    DrawText2(m.dc, lead, p2, g_fSmall, P.muted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    c.link = { p2.left + lw, p2.top, p2.left + lw + kw, p2.bottom };
    DrawText2(m.dc, kRepoShort, c.link, g_fSmallBold, c.linkHot ? P.accentHot : P.accent, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (c.linkHot) {
        int uy = (c.link.top + c.link.bottom) / 2 + S(8);
        RECT ul{ c.link.left, uy, c.link.right, uy + 1 };
        HBRUSH ub = CreateSolidBrush(P.accentHot);
        FillRect(m.dc, &ul, ub);
        DeleteObject(ub);
    }
    RECT cr{ c.link.right + S(14), p2.top, p2.right, p2.bottom };
    DrawText2(m.dc, TR(L"\u00b7  GPL-3.0  \u00b7  basato su yt-dlp e FFmpeg"), cr, g_fSmall, P.muted, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    m.Blit(0, 0);
}

static void AboutRefreshButtons(AboutCtx& c) {
    bool missing = false;
    for (auto& k : c.comp) if (k.path.empty()) missing = true;
    SetWindowTextW(c.update, missing ? TR(L"Scarica componenti") : TR(L"Aggiorna yt-dlp"));
    EnableWindow(c.update, !g_toolBusy);
    InvalidateRect(c.update, nullptr, FALSE);
}

static void AboutLayout(HWND dlg, AboutCtx& c) {
    RECT rc;
    GetClientRect(dlg, &rc);
    AboutGeo g = AboutLayoutGeo(rc);
    int y = g.buttons.top, h = g.buttons.bottom - g.buttons.top, x = g.buttons.right;
    int wc = TextWidth(g_fUi, TR(L"Chiudi")) + S(40);
    MoveWindow(c.close, x - wc, y, wc, h, FALSE); x -= wc + S(8);
    int wu = TextWidth(g_fBold, TR(L"Scarica componenti")) + S(58);
    MoveWindow(c.update, x - wu, y, wu, h, FALSE);
    int wt = TextWidth(g_fUi, TR(L"Strumenti")) + S(56);
    MoveWindow(c.tools, g.buttons.left, y, wt, h, FALSE);
    int wcp = TextWidth(g_fUi, TR(L"Copia")) + S(56);
    MoveWindow(c.copy, g.buttons.left + wt + S(8), y, wcp, h, FALSE);
}

static INT_PTR CALLBACK AboutProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* c = reinterpret_cast<AboutCtx*>(GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg) {
    case WM_INITDIALOG: {
        c = new AboutCtx;
        SetWindowLongPtrW(dlg, DWLP_USER, (LONG_PTR)c);
        TranslateWindow(dlg);
        SendMessageW(dlg, WM_SETICON, ICON_BIG, (LPARAM)g_appIcon);
        c->fHero = MkFont(L"Segoe UI", 24, FW_SEMIBOLD);
        c->fSect = MkFont(L"Segoe UI", 8, FW_BOLD);
        c->comp[0].path = ResolveTool(g_set.ytdlp, L"yt-dlp.exe");
        c->comp[1].path = ResolveTool(g_set.ffmpeg, L"ffmpeg.exe");
        c->comp[2].path = ResolveTool(L"", L"deno.exe");
        auto mk = [&](const wchar_t* text, int id) {
            HWND h = CreateWindowExW(0, WC_BUTTONW, text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10,
                                     dlg, (HMENU)(INT_PTR)id, g_inst, nullptr);
            SendMessageW(h, WM_SETFONT, (WPARAM)g_fUi, FALSE);
            SetWindowSubclass(h, BtnSubclass, 1, 0);
            return h;
        };
        c->tools = mk(TR(L"Strumenti"), IDC_AB_TOOLS);
        c->copy = mk(TR(L"Copia"), IDC_AB_COPY);
        c->update = mk(TR(L"Aggiorna yt-dlp"), IDC_AB_UPDATE);
        c->close = mk(TR(L"Chiudi"), IDCANCEL);
        SetTitleBarDark(dlg, g_dark);
        RECT wr{ 0, 0, S(600), S(500) };
        AdjustWindowRectEx(&wr, (DWORD)GetWindowLongW(dlg, GWL_STYLE), FALSE, (DWORD)GetWindowLongW(dlg, GWL_EXSTYLE));
        RECT pr;
        GetWindowRect(g_hwnd, &pr);
        int ww = wr.right - wr.left, wh = wr.bottom - wr.top;
        SetWindowPos(dlg, nullptr, (pr.left + pr.right - ww) / 2, std::max<LONG>(0, (pr.top + pr.bottom - wh) / 2), ww, wh,
                     SWP_NOZORDER);
        AboutLayout(dlg, *c);
        AboutRefreshButtons(*c);
        // versioni in background (yt-dlp impiega un attimo ad avviarsi)
        auto* job = new AboutJob{ dlg, { c->comp[0].path, c->comp[1].path, c->comp[2].path } };
        if (!TrySubmitThreadpoolCallback(AboutWork, job, nullptr)) delete job;
        SetFocus(c->close);
        return FALSE;
    }
    case WM_APP_ABOUTVER: {
        std::unique_ptr<AboutVers> v(reinterpret_cast<AboutVers*>(lp));
        if (c) {
            for (int i = 0; i < 3; ++i) { c->comp[i].version = v->v[i]; c->comp[i].checked = true; }
            InvalidateRect(dlg, nullptr, FALSE);
        }
        return TRUE;
    }
    case WM_MOUSEMOVE:
        if (c) {
            POINT pt{ (short)LOWORD(lp), (short)HIWORD(lp) };
            bool hot = PtInRect(&c->link, pt) != 0;
            if (hot != c->linkHot) {
                c->linkHot = hot;
                RECT r = c->link;
                InflateRect(&r, S(2), S(4));
                InvalidateRect(dlg, &r, FALSE);
                if (hot) { TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, dlg, 0 }; TrackMouseEvent(&t); }
            }
        }
        return TRUE;
    case WM_MOUSELEAVE:
        if (c && c->linkHot) { c->linkHot = false; InvalidateRect(dlg, nullptr, FALSE); }
        return TRUE;
    case WM_SETCURSOR:
        if (c && c->linkHot && LOWORD(lp) == HTCLIENT) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            SetWindowLongPtrW(dlg, DWLP_MSGRESULT, TRUE);
            return TRUE;
        }
        break;
    case WM_LBUTTONUP:
        if (c) {
            POINT pt{ (short)LOWORD(lp), (short)HIWORD(lp) };
            if (PtInRect(&c->link, pt)) ShellExecuteW(dlg, L"open", kRepoUrl, nullptr, nullptr, SW_SHOWNORMAL);
        }
        return TRUE;
    case WM_ERASEBKGND:
        return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(dlg, &ps);
        if (c) AboutPaint(dlg, dc, *c);
        EndPaint(dlg, &ps);
        return TRUE;
    }
    case WM_DRAWITEM:
        DrawButton(reinterpret_cast<DRAWITEMSTRUCT*>(lp));
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_AB_TOOLS:
            ShellExecuteW(dlg, L"open", BinDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return TRUE;
        case IDC_AB_COPY:
            SetClipboardText(AboutText(*c));
            SetWindowTextW(c->copy, TR(L"Copiato"));
            InvalidateRect(c->copy, nullptr, FALSE);
            return TRUE;
        case IDC_AB_UPDATE: {
            int tasks = TT_YTDLP;
            if (c->comp[1].path.empty()) tasks |= TT_FFMPEG;
            if (c->comp[2].path.empty()) tasks |= TT_DENO;
            EndDialog(dlg, IDOK);
            RunTool(tasks);
            return TRUE;
        }
        case IDOK:
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    case WM_DESTROY:
        if (c) {
            if (c->fHero) DeleteObject(c->fHero);
            if (c->fSect) DeleteObject(c->fSect);
            delete c;
            SetWindowLongPtrW(dlg, DWLP_USER, 0);
        }
        break;
    }
    return FALSE;
}
