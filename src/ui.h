// Stackly - tema, disegno, pulsanti, card della coda
#pragma once
#include "engine.h"

// ---------------------------------------------------------------------------
// Tema chiaro / scuro
// ---------------------------------------------------------------------------
static bool SystemIsDark() {
    DWORD v = 1, sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &sz) == ERROR_SUCCESS)
        return v == 0;
    return false;
}

// API non documentate di uxtheme per menu e barre di scorrimento scure (usate da Explorer, Notepad++...)
static void SetAppDarkMode(bool dark) {
    static HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!ux) return;
    using FnMode = int(WINAPI*)(int);
    using FnVoid = void(WINAPI*)();
    auto setMode = reinterpret_cast<FnMode>(reinterpret_cast<void*>(GetProcAddress(ux, MAKEINTRESOURCEA(135))));
    auto flush = reinterpret_cast<FnVoid>(reinterpret_cast<void*>(GetProcAddress(ux, MAKEINTRESOURCEA(136))));
    auto refresh = reinterpret_cast<FnVoid>(reinterpret_cast<void*>(GetProcAddress(ux, MAKEINTRESOURCEA(104))));
    if (setMode) setMode(dark ? 2 : 3);   // ForceDark / ForceLight
    if (refresh) refresh();
    if (flush) flush();
}

static void SetTitleBarDark(HWND h, bool dark) {
    BOOL v = dark ? TRUE : FALSE;
    // 20 = DWMWA_USE_IMMERSIVE_DARK_MODE (Windows 10 2004+), 19 = valore delle build precedenti
    if (FAILED(DwmSetWindowAttribute(h, (DWMWINDOWATTRIBUTE)20, &v, sizeof(v))))
        DwmSetWindowAttribute(h, (DWMWINDOWATTRIBUTE)19, &v, sizeof(v));
}

static void MakeBrushes() {
    for (HBRUSH* b : { &g_brBg, &g_brHeader, &g_brField }) if (*b) { DeleteObject(*b); *b = nullptr; }
    g_brBg = CreateSolidBrush(P.bg);
    g_brHeader = CreateSolidBrush(P.header);
    g_brField = CreateSolidBrush(P.field);
}

static void ResolveTheme() {
    g_dark = g_set.theme == 2 || (g_set.theme == 0 && SystemIsDark());
    P = g_dark ? kDark : kLight;
    MakeBrushes();
    SetAppDarkMode(g_dark);
}

// ---------------------------------------------------------------------------
// Font
// ---------------------------------------------------------------------------
static int CALLBACK FontExistsProc(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM lp) {
    *reinterpret_cast<bool*>(lp) = true;
    return 0;
}
static bool FontExists(const wchar_t* face) {
    LOGFONTW lf{};
    lf.lfCharSet = DEFAULT_CHARSET;
    wcsncpy_s(lf.lfFaceName, face, _TRUNCATE);
    bool found = false;
    HDC dc = GetDC(nullptr);
    EnumFontFamiliesExW(dc, &lf, FontExistsProc, (LPARAM)&found, 0);
    ReleaseDC(nullptr, dc);
    return found;
}

static HFONT MkFont(const wchar_t* face, int pt, int weight) {
    LOGFONTW lf{};
    lf.lfHeight = -MulDiv(pt, (int)g_dpi, 72);
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcsncpy_s(lf.lfFaceName, face, _TRUNCATE);
    return CreateFontIndirectW(&lf);
}

static void MakeFonts() {
    for (HFONT* f : { &g_fUi, &g_fBold, &g_fSmall, &g_fSmallBold, &g_fTitle, &g_fHead, &g_fIcon, &g_fIconSm, &g_fIconMd, &g_fIconLg })
        if (*f) { DeleteObject(*f); *f = nullptr; }
    const wchar_t* ui = L"Segoe UI";
    g_fUi        = MkFont(ui, 10, FW_NORMAL);
    g_fBold      = MkFont(ui, 10, FW_SEMIBOLD);
    g_fSmall     = MkFont(ui, 9, FW_NORMAL);
    g_fSmallBold = MkFont(ui, 8, FW_BOLD);
    g_fTitle     = MkFont(ui, 13, FW_SEMIBOLD);
    g_fHead      = MkFont(ui, 15, FW_SEMIBOLD);
    g_fIcon      = MkFont(g_iconFace.c_str(), 11, FW_NORMAL);
    g_fIconSm    = MkFont(g_iconFace.c_str(), 9, FW_NORMAL);
    g_fIconMd    = MkFont(g_iconFace.c_str(), 18, FW_NORMAL);
    g_fIconLg    = MkFont(g_iconFace.c_str(), 36, FW_NORMAL);
}

static int TextWidth(HFONT f, const std::wstring& s) {
    HDC dc = GetDC(g_hwnd);
    HGDIOBJ old = SelectObject(dc, f);
    SIZE sz{};
    GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
    SelectObject(dc, old);
    ReleaseDC(g_hwnd, dc);
    return sz.cx;
}

// ---------------------------------------------------------------------------
// Primitive di disegno (GDI+ con antialiasing per le forme, GDI ClearType per il testo)
// ---------------------------------------------------------------------------
static Gdiplus::Color GC(COLORREF c, BYTE a = 255) {
    return Gdiplus::Color(a, GetRValue(c), GetGValue(c), GetBValue(c));
}

static COLORREF Mix(COLORREF a, COLORREF b, double t) {
    return RGB((int)(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t),
               (int)(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t),
               (int)(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t));
}

static void RoundPath(Gdiplus::GraphicsPath& p, const RECT& r, float rad) {
    float x = (float)r.left, y = (float)r.top, w = (float)(r.right - r.left), h = (float)(r.bottom - r.top);
    float d = std::min(rad * 2, std::min(w, h));
    if (d <= 0.5f) { p.AddRectangle(Gdiplus::RectF(x, y, w, h)); return; }
    p.AddArc(x, y, d, d, 180, 90);
    p.AddArc(x + w - d, y, d, d, 270, 90);
    p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    p.AddArc(x, y + h - d, d, d, 90, 90);
    p.CloseFigure();
}

static void FillRound(Gdiplus::Graphics& g, const RECT& r, float rad, COLORREF c, BYTE a = 255) {
    Gdiplus::GraphicsPath p;
    RoundPath(p, r, rad);
    Gdiplus::SolidBrush b(GC(c, a));
    g.FillPath(&b, &p);
}

static void StrokeRound(Gdiplus::Graphics& g, const RECT& r, float rad, COLORREF c, float w = 1.0f, BYTE a = 255) {
    RECT rr = r;
    rr.right -= 1;
    rr.bottom -= 1;
    Gdiplus::GraphicsPath p;
    RoundPath(p, rr, rad);
    Gdiplus::Pen pen(GC(c, a), w);
    g.DrawPath(&pen, &p);
}

// Riempimento con gradiente orizzontale (pulsante principale, barre di avanzamento)
static void FillRoundGrad(Gdiplus::Graphics& g, const RECT& r, float rad, COLORREF a, COLORREF b, BYTE alpha = 255,
                          bool vertical = false) {
    Gdiplus::GraphicsPath p;
    RoundPath(p, r, rad);
    Gdiplus::RectF rf((float)r.left - 1, (float)r.top - 1, (float)(r.right - r.left) + 2, (float)(r.bottom - r.top) + 2);
    Gdiplus::LinearGradientBrush br(rf, GC(a, alpha), GC(b, alpha),
                                    vertical ? Gdiplus::LinearGradientModeVertical : Gdiplus::LinearGradientModeHorizontal);
    g.FillPath(&br, &p);
}

static COLORREF Lighten(COLORREF c, double t) { return Mix(c, RGB(255, 255, 255), t); }
static COLORREF Darken(COLORREF c, double t) { return Mix(c, RGB(0, 0, 0), t); }

static void PrepG(Gdiplus::Graphics& g) {
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
}

static void DrawText2(HDC dc, const std::wstring& s, RECT r, HFONT f, COLORREF c, UINT fmt) {
    HGDIOBJ old = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    DrawTextW(dc, s.c_str(), (int)s.size(), &r, fmt | DT_NOPREFIX);
    SelectObject(dc, old);
}

static void DrawGlyph(HDC dc, wchar_t g, RECT r, HFONT f, COLORREF c) {
    wchar_t s[2] = { g, 0 };
    DrawText2(dc, s, r, f, c, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// Buffer di disegno senza sfarfallio (coordinate locali 0,0)
struct MemDC {
    HDC target, dc;
    HBITMAP bmp, old;
    int w, h;
    MemDC(HDC t, int width, int height) : target(t), w(std::max(width, 1)), h(std::max(height, 1)) {
        dc = CreateCompatibleDC(t);
        bmp = CreateCompatibleBitmap(t, w, h);
        old = (HBITMAP)SelectObject(dc, bmp);
    }
    void Blit(int x, int y) { BitBlt(target, x, y, w, h, dc, 0, 0, SRCCOPY); }
    ~MemDC() {
        SelectObject(dc, old);
        DeleteObject(bmp);
        DeleteDC(dc);
    }
};

// ---------------------------------------------------------------------------
// Pulsanti owner-drawn
// ---------------------------------------------------------------------------
// Stile sobrio: angoli appena arrotondati, bordi sottili, colore solo dove serve
enum BtnKind { BK_PRIMARY, BK_SECONDARY, BK_GHOST, BK_FIELD, BK_CHECK, BK_FOOT };

struct BtnSpec {
    BtnKind kind = BK_SECONDARY;
    wchar_t glyph = 0;
    bool on = false;
    bool pathEllipsis = false;
    bool rect = false;      // rettangolo ad angoli appena smussati invece della pillola (Impostazioni)
    COLORREF behind = 0;
};

// Casella di spunta unica per tutta l'app (pulsanti-spunta e liste): quadrato con spunta tracciata
static const int kCheckSize = 16;
static void DrawCheckBox(Gdiplus::Graphics& g, const RECT& box, bool on, bool hot, bool disabled) {
    const float rad = (float)S(3);
    const float sz = (float)(box.right - box.left);
    if (on && !disabled) {
        FillRoundGrad(g, box, rad, hot ? Lighten(P.gradA, 0.1) : P.gradA, hot ? Lighten(P.gradB, 0.1) : P.gradB);
        Gdiplus::Pen pen(Gdiplus::Color(255, 255, 255, 255), std::max(1.5f, sz * 0.13f));
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        pen.SetLineJoin(Gdiplus::LineJoinRound);
        float x = (float)box.left, y = (float)box.top;
        Gdiplus::PointF pts[3] = { { x + sz * 0.25f, y + sz * 0.52f }, { x + sz * 0.42f, y + sz * 0.69f },
                                   { x + sz * 0.75f, y + sz * 0.33f } };
        g.DrawLines(&pen, pts, 3);
    } else {
        FillRound(g, box, rad, disabled ? Mix(P.field, P.bg, 0.5) : P.field);
        COLORREF bc = disabled ? Mix(P.border, P.bg, 0.4) : (hot ? Mix(P.border, P.accent, 0.6) : Mix(P.border, P.muted, 0.35));
        StrokeRound(g, box, rad, bc, 1.3f);
    }
}

static wchar_t KindGlyph(int k) {
    switch (k) {
    case DK_AUDIO: return G_AUDIO;
    case DK_SUBS: return G_CC;
    case DK_MANUAL: return G_FILTER;
    default: return G_VIDEO;
    }
}

static wchar_t ThemeGlyph() { return g_set.theme == 0 ? G_AUTO : (g_set.theme == 1 ? G_SUN : G_MOON); }

static BtnSpec SpecFor(HWND h) {
    BtnSpec s;
    s.behind = P.header;
    s.rect = GetPropW(h, L"stk.rect") != nullptr;
    switch (GetDlgCtrlID(h)) {
    case IDC_B_PASTE:    s.kind = BK_GHOST; s.glyph = G_PASTE; s.behind = P.field; break;
    case IDC_B_FORMATS:  s.kind = BK_GHOST; s.glyph = G_FILTER; s.behind = P.field; break;
    case IDC_B_DOWNLOAD: s.kind = BK_PRIMARY; s.glyph = G_DOWNLOAD; break;
    case IDC_B_THEME:    s.kind = BK_GHOST; s.glyph = ThemeGlyph(); break;
    case IDC_B_SETTINGS: s.kind = BK_GHOST; s.glyph = G_SETTINGS; break;
    case IDC_B_MORE:     s.kind = BK_GHOST; s.glyph = G_MORE; break;
    case IDC_B_PROFILE:  s.kind = BK_FIELD; s.glyph = KindGlyph(g_set.preset.kind); break;
    case IDC_B_FOLDER:   s.kind = BK_FOOT; s.glyph = G_FOLDER; s.pathEllipsis = true; break;
    // finestra Formati
    case IDOK:           s.kind = BK_PRIMARY; s.glyph = GetPropW(h, L"stk.noglyph") ? 0 : G_DOWNLOAD; s.behind = P.bg; break;
    // Impostazioni
    case IDC_SET_YTDLP_BR:
    case IDC_SET_FFMPEG_BR: s.kind = BK_SECONDARY; s.behind = P.bg; break;
    case IDCANCEL:
    case IDC_PK_ADV:     s.kind = BK_SECONDARY; s.behind = P.bg; break;
    case IDC_PK_CONT:    s.kind = BK_FIELD; s.behind = P.bg; break;
    case IDC_AB_UPDATE:  s.kind = BK_PRIMARY; s.glyph = G_REFRESH; s.behind = P.bg; break;
    case IDC_AB_TOOLS:   s.kind = BK_SECONDARY; s.glyph = G_FOLDER; s.behind = P.bg; break;
    case IDC_AB_COPY:    s.kind = BK_SECONDARY; s.glyph = G_COPY; s.behind = P.bg; break;
    case IDC_PK_EMBED:
    case IDC_PK_SRT:
    case IDC_SET_SEGMENTED:   // spunte delle Impostazioni: stesse caselle della finestra Formati
    case IDC_SET_AUTOSUBS:
    case IDC_SET_TRAY:     s.kind = BK_CHECK; s.on = GetWindowLongPtrW(h, GWLP_USERDATA) != 0; s.behind = P.bg; break;
    }
    return s;
}

static void DrawButton(const DRAWITEMSTRUCT* dis) {
    HWND h = dis->hwndItem;
    RECT rc = dis->rcItem;
    int w = rc.right - rc.left, hgt = rc.bottom - rc.top;
    BtnSpec s = SpecFor(h);
    bool hot = g_hotBtn == h;
    bool down = (dis->itemState & ODS_SELECTED) != 0;
    bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    bool focus = (dis->itemState & ODS_FOCUS) && !(dis->itemState & ODS_NOFOCUSRECT);

    MemDC m(dis->hDC, w, hgt);
    RECT r{ 0, 0, w, hgt };
    HBRUSH bb = CreateSolidBrush(s.behind);
    FillRect(m.dc, &r, bb);
    DeleteObject(bb);

    COLORREF fg = disabled ? Mix(P.muted, s.behind, 0.35) : P.text;
    const float pill = s.rect ? (float)S(4) : hgt / 2.0f;   // pillola, o rettangolo nelle Impostazioni
    const float rad = (float)S(9);                 // pulsanti a icona: quadrati morbidi
    COLORREF glyphCol = 0;                         // colore del glifo se diverso dal testo
    RECT box{};   // casella di spunta
    {
        Gdiplus::Graphics g(m.dc);
        PrepG(g);
        switch (s.kind) {
        case BK_PRIMARY: {
            // gradiente magenta -> rubino, piu' chiaro al passaggio, piu' scuro alla pressione
            COLORREF a = P.gradA, b = P.gradB;
            if (disabled) { a = Mix(a, s.behind, 0.55); b = Mix(b, s.behind, 0.55); }
            else if (down) { a = Darken(a, 0.16); b = Darken(b, 0.16); }
            else if (hot) { a = Lighten(a, 0.12); b = Lighten(b, 0.12); }
            FillRoundGrad(g, r, pill, a, b);
            if (!disabled && !down) {   // riflesso morbido sulla meta' alta
                Gdiplus::GraphicsPath clip;
                RoundPath(clip, r, pill);
                g.SetClip(&clip);
                RECT top{ r.left, r.top, r.right, r.top + hgt / 2 };
                FillRoundGrad(g, top, 0, RGB(255, 255, 255), RGB(255, 255, 255), 30, true);
                g.ResetClip();
            }
            fg = disabled ? Mix(P.onAccent, s.behind, 0.3) : P.onAccent;
            break;
        }
        case BK_SECONDARY:
            FillRound(g, r, pill, down ? P.border : (hot ? Mix(P.surface2, P.border, 0.55) : P.surface2));
            if (!g_dark) StrokeRound(g, r, pill, P.border);
            break;
        case BK_GHOST:
        case BK_FOOT:
            if (hot || down) {
                FillRound(g, r, s.kind == BK_FOOT ? pill : rad, P.accent, down ? 70 : 38);
                if (!disabled) glyphCol = g_dark ? P.accentHot : P.accent;
            }
            break;
        case BK_FIELD:
            FillRound(g, r, pill, disabled ? s.behind : (down ? P.surface2 : P.field));
            StrokeRound(g, r, pill, (hot && !disabled) ? Mix(P.border, P.accent, 0.6) : P.border, hot ? 1.3f : 1.0f);
            break;
        case BK_CHECK: {
            int bs = S(kCheckSize);
            box = { 0, (hgt - bs) / 2, bs, (hgt - bs) / 2 + bs };
            DrawCheckBox(g, box, s.on, hot, disabled);
            break;
        }
        }
        if (focus) {
            RECT fr = s.kind == BK_CHECK ? box : r;
            InflateRect(&fr, -1, -1);
            float fra = s.kind == BK_CHECK ? (float)S(3) : ((s.kind == BK_GHOST) ? rad : pill);
            StrokeRound(g, fr, fra, s.kind == BK_PRIMARY ? RGB(255, 255, 255) : P.accent, 2.0f, 190);
        }
    }

    std::wstring text = GetText(h);
    if (s.kind == BK_CHECK) {
        RECT tr{ box.right + S(8), 0, w, hgt };
        DrawText2(m.dc, text, tr, g_fUi, fg, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    } else if (s.kind == BK_FOOT) {
        RECT gr{ S(8), 0, S(26), hgt };
        DrawGlyph(m.dc, s.glyph, gr, g_fIconSm, glyphCol ? glyphCol : P.muted);
        RECT tr{ S(32), 0, w - S(8), hgt };
        DrawText2(m.dc, text, tr, g_fSmall, hot ? P.text : P.muted,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS);
    } else if (s.kind == BK_FIELD) {
        int x = S(14);
        if (s.glyph) {
            RECT gr{ x, 0, x + S(18), hgt };
            DrawGlyph(m.dc, s.glyph, gr, g_fIcon, disabled ? fg : P.accent);   // icona del campo in tinta
            x += S(26);
        }
        RECT tr{ x, 0, w - S(32), hgt };
        DrawText2(m.dc, text, tr, g_fUi, fg,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | (s.pathEllipsis ? DT_PATH_ELLIPSIS : DT_END_ELLIPSIS));
        RECT cr{ w - S(30), 0, w - S(10), hgt };
        DrawGlyph(m.dc, G_CHEVRON, cr, g_fIconSm, disabled ? fg : P.muted);
    } else if (text.empty()) {
        DrawGlyph(m.dc, s.glyph, r, g_fIcon, glyphCol ? glyphCol : fg);
    } else {
        HFONT tf = s.kind == BK_PRIMARY ? g_fBold : g_fUi;
        const int gw = s.glyph ? S(16) : 0, gap = s.glyph ? S(8) : 0;
        int tw = std::min(TextWidth(tf, text), std::max(w - S(20) - gw - gap, 0));
        int x = (w - (gw + gap + tw)) / 2;
        if (s.glyph) {
            RECT gr{ x, 0, x + gw, hgt };
            DrawGlyph(m.dc, s.glyph, gr, g_fIcon, fg);
            x += gw + gap;
        }
        RECT tr{ x, 0, x + tw, hgt };
        DrawText2(m.dc, text, tr, tf, fg, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    m.Blit(rc.left, rc.top);
}

// Effetto "hover" dei pulsanti
static LRESULT CALLBACK BtnSubclass(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    switch (msg) {
    case WM_MOUSEMOVE:
        if (g_hotBtn != h) {
            HWND old = g_hotBtn;
            g_hotBtn = h;
            if (old) InvalidateRect(old, nullptr, FALSE);
            InvalidateRect(h, nullptr, FALSE);
            TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, h, 0 };
            TrackMouseEvent(&t);
        }
        break;
    case WM_MOUSELEAVE:
        if (g_hotBtn == h) { g_hotBtn = nullptr; InvalidateRect(h, nullptr, FALSE); }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_LBUTTONDBLCLK:   // doppio clic = due clic, niente BN_DOUBLECLICKED perso
        SendMessageW(h, WM_LBUTTONDOWN, wp, lp);
        return 0;
    case WM_NCDESTROY:
        if (g_hotBtn == h) g_hotBtn = nullptr;
        break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Card della coda
// ---------------------------------------------------------------------------
static int ItemHeight() { return S(84); }

// Riga della coda in stile Stacher: miniatura, titolo + stato, dettagli, barra sottile, azioni a icona
struct CardGeo {
    RECT card, thumb, title, line2, bar, status, action, remove;
};

static CardGeo Geo(const RECT& rc) {
    CardGeo g{};
    g.card = rc;
    InflateRect(&g.card, -S(14), -S(3));
    const int pad = S(10);
    int th = (g.card.bottom - g.card.top) - 2 * pad;
    int tw = th * 16 / 9;
    g.thumb = { g.card.left + pad, g.card.top + pad, g.card.left + pad + tw, g.card.top + pad + th };
    int cy = (g.card.top + g.card.bottom) / 2;
    const int bs = S(32);
    g.remove = { g.card.right - pad - bs, cy - bs / 2, g.card.right - pad, cy + bs / 2 };
    g.action = { g.remove.left - S(4) - bs, cy - bs / 2, g.remove.left - S(4), cy + bs / 2 };
    int tx = g.thumb.right + S(14);
    int rx = g.action.left - S(16);
    g.status = { std::max(tx, rx - S(150)), g.thumb.top, rx, g.thumb.top + S(22) };
    g.title = { tx, g.thumb.top, g.status.left - S(10), g.thumb.top + S(22) };
    g.line2 = { tx, g.title.bottom + S(1), rx, g.title.bottom + S(21) };
    g.bar = { tx, g.thumb.bottom - S(4), rx, g.thumb.bottom };
    return g;
}

// Etichetta coerente "Tipo · dettagli" (coda e profilo)
static std::wstring ShortQuality(const Preset& p) {
    if (p.height > 0) return std::to_wstring(p.height) + L"p";
    if (p.vq == kAskQuality) return L"Chiedi";
    std::wstring q = kVideoQ[p.vq].value;
    if (size_t sp = q.find(L" ("); sp != std::wstring::npos) q.resize(sp);
    return q;
}

static std::wstring PresetLabel(const Preset& p, int container, bool subs = false) {
    const wchar_t* dot = L" \u00b7 ";
    switch (p.kind) {
    case DK_AUDIO: {
        std::wstring s = std::wstring(L"Solo audio") + dot + kAudioFmts[p.afmt].value;
        if (p.afmt != 2) s += dot + std::wstring(kAudioQ[p.aq].value);
        return s;
    }
    case DK_SUBS:
        return std::wstring(L"Solo sottotitoli") + dot + kSubFmts[p.subfmt].value;
    case DK_MANUAL:
        return L"Scegli ogni volta";
    case DK_VIDEOONLY:
        return std::wstring(L"Solo video") + dot + ShortQuality(p) + dot + kContainers[container].value;
    default:
        return std::wstring(L"Video + audio") + dot + ShortQuality(p) + dot + kContainers[container].value +
               (subs ? std::wstring(dot) + L"Sub" : L"");
    }
}



static std::wstring ItemFormatLabel(const Item& it) {
    return it.custom ? it.customLabel : PresetLabel(it.preset, it.container, it.subs);
}

static void DrawCard(HDC target, const RECT& itemRc, const Item& it, bool selected, bool hot, int hotPart, bool focused) {
    int w = itemRc.right - itemRc.left, h = itemRc.bottom - itemRc.top;
    MemDC m(target, w, h);
    RECT local{ 0, 0, w, h };
    FillRect(m.dc, &local, g_brBg);
    CardGeo g = Geo(local);

    // stato -> colori e testi
    COLORREF stColor = P.muted;
    std::wstring stText;
    wchar_t action = G_CANCEL;
    switch (it.st) {
    case St::Queued:      stText = L"In coda"; break;
    case St::Starting:    stText = L"Avvio..."; break;
    case St::Downloading: stText = it.pct.empty() ? L"Download..." : it.pct; stColor = P.text; break;
    case St::Processing:  stText = L"Creazione file..."; stColor = P.text; break;
    case St::Done:        stText = L"Completato"; stColor = P.green; action = G_OPENFOLDER; break;
    case St::Error:       stText = L"Errore"; stColor = P.red; action = G_REFRESH; break;
    case St::Cancelled:   stText = L"Annullato"; action = G_REFRESH; break;
    }

    {
        Gdiplus::Graphics gr(m.dc);
        PrepG(gr);
        COLORREF cardCol = selected ? Mix(P.surface, P.surface2, 0.7) : (hot ? Mix(P.surface, P.surface2, 0.4) : P.surface);
        FillRound(gr, g.card, (float)S(8), cardCol);
        if (!g_dark) StrokeRound(gr, g.card, (float)S(8), selected && focused ? P.accent : Mix(P.border, P.surface, 0.3));
        else if (selected && focused) StrokeRound(gr, g.card, (float)S(8), P.accent, 1.0f, 200);
        if (selected) {   // segno d'accento a sinistra, come la riga attiva di Stacher
            RECT mk{ g.card.left, g.card.top + S(14), g.card.left + S(3), g.card.bottom - S(14) };
            FillRoundGrad(gr, mk, (float)S(1), P.gradA, P.gradB, 255, true);
        }

        // miniatura
        if (it.thumb) {
            Gdiplus::GraphicsPath p;
            RoundPath(p, g.thumb, (float)S(4));
            gr.SetClip(&p);
            gr.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            gr.DrawImage(it.thumb.get(), Gdiplus::Rect(g.thumb.left, g.thumb.top, g.thumb.right - g.thumb.left,
                                                       g.thumb.bottom - g.thumb.top));
            gr.ResetClip();
        } else {
            FillRound(gr, g.thumb, (float)S(4), P.surface2);
        }

        // barra di avanzamento
        FillRound(gr, g.bar, (float)S(2), P.track);
        double pct = it.st == St::Done ? 100.0 : it.pctValue;
        if (it.st == St::Processing) pct = 100.0;
        if (pct > 0) {
            RECT f = g.bar;
            f.right = f.left + std::max((int)((g.bar.right - g.bar.left) * pct / 100.0), S(6));
            if (it.st == St::Downloading || it.st == St::Processing || it.st == St::Starting) {
                FillRoundGrad(gr, f, (float)S(2), P.gradA, P.gradB);
            } else {
                COLORREF fc = it.st == St::Done ? P.green : (it.st == St::Error ? P.red : P.muted);
                FillRound(gr, f, (float)S(2), fc);
            }
        }

        // pulsanti a icona (sfondo solo al passaggio del mouse)
        if (hotPart == 1) FillRound(gr, g.action, (float)S(6), g_dark ? P.border : P.surface2);
        if (hotPart == 2 && !IsActive(it)) FillRound(gr, g.remove, (float)S(6), g_dark ? P.border : P.surface2);

        // badge playlist
        if (it.plCount > 0) {
            std::wstring b = std::to_wstring(it.plIdx) + L"/" + std::to_wstring(it.plCount);
            int bw = TextWidth(g_fSmallBold, b) + S(12);
            RECT br{ g.thumb.right - bw - S(4), g.thumb.bottom - S(20), g.thumb.right - S(4), g.thumb.bottom - S(4) };
            FillRound(gr, br, (float)S(3), RGB(0, 0, 0), 180);
        }
    }

    if (!it.thumb) {
        wchar_t ph = ItemIsAudio(it) ? G_AUDIO
                     : (((it.custom && it.customKind == 2) || (!it.custom && it.preset.kind == DK_SUBS)) ? G_CC : G_VIDEO);
        DrawGlyph(m.dc, ph, g.thumb, g_fIconMd, P.muted);
    }
    if (it.plCount > 0) {
        std::wstring b = std::to_wstring(it.plIdx) + L"/" + std::to_wstring(it.plCount);
        int bw = TextWidth(g_fSmallBold, b) + S(12);
        RECT br{ g.thumb.right - bw - S(4), g.thumb.bottom - S(20), g.thumb.right - S(4), g.thumb.bottom - S(4) };
        DrawText2(m.dc, b, br, g_fSmallBold, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    DrawText2(m.dc, it.title.empty() ? it.url : it.title, g.title, g_fBold, P.text,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    DrawText2(m.dc, stText, g.status, it.st == St::Downloading ? g_fBold : g_fUi, stColor,
              DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    std::wstring l2;
    COLORREF l2c = P.muted;
    if (it.st == St::Error && !it.err.empty()) {
        l2 = it.err;
        l2c = P.red;
    } else {
        auto add = [&](const std::wstring& s) { if (!s.empty()) { if (!l2.empty()) l2 += L"   ·   "; l2 += s; } };
        add(ItemFormatLabel(it));
        add(it.size);
        if (it.st == St::Downloading) {
            add(it.speed);
            if (!it.eta.empty()) add(L"mancano " + it.eta);
            if (it.usingNative) add(L"multi-connessione");
        }
        if (it.st == St::Done && !it.filePath.empty()) add(it.filePath.substr(it.filePath.find_last_of(L'\\') + 1));
    }
    DrawText2(m.dc, l2, g.line2, g_fSmall, l2c, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    DrawGlyph(m.dc, action, g.action, g_fIcon, hotPart == 1 ? P.text : (hot || selected ? P.text : P.muted));
    if (!IsActive(it))
        DrawGlyph(m.dc, G_DELETE, g.remove, g_fIcon, hotPart == 2 ? P.red : (hot || selected ? P.text : P.muted));

    m.Blit(itemRc.left, itemRc.top);
}
