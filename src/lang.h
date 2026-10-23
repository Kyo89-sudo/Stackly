// Stackly - localizzazione
// I testi sono scritti in italiano nel codice e passano da TR(). In inglese TR() li cerca nella
// tabella generata (lang_table.inc, da tools/i18n.py): se una voce mancasse resta l'italiano.
#pragma once
#include <windows.h>
#include <string>
#include <unordered_map>

struct TrPair {
    const wchar_t* it;
    const wchar_t* en;
};

static const TrPair kTrTable[] = {
#include "lang_table.inc"
};

static bool g_en = false;   // true = interfaccia in inglese

// Lingua: 0 = come Windows, 1 = italiano, 2 = inglese
static void SetLanguage(int pref) {
    if (pref == 1) g_en = false;
    else if (pref == 2) g_en = true;
    else g_en = PRIMARYLANGID(GetUserDefaultUILanguage()) != LANG_ITALIAN;
}

static const wchar_t* TR(const wchar_t* it) {
    if (!g_en || !it || !*it) return it;
    static const std::unordered_map<std::wstring, const wchar_t*> map = [] {
        std::unordered_map<std::wstring, const wchar_t*> m;
        for (const TrPair& p : kTrTable) m.emplace(p.it, p.en);
        return m;
    }();
    auto f = map.find(it);
    return f == map.end() ? it : f->second;
}

// Traduce i testi dei controlli di una finestra creata da risorsa (etichette, caselle, pulsanti)
static BOOL CALLBACK TrChildProc(HWND h, LPARAM) {
    wchar_t buf[512];
    if (GetWindowTextW(h, buf, 512) > 0) {
        const wchar_t* t = TR(buf);
        if (t != buf) SetWindowTextW(h, t);
    }
    return TRUE;
}
static void TranslateWindow(HWND dlg) {
    if (!g_en) return;
    TrChildProc(dlg, 0);
    EnumChildWindows(dlg, TrChildProc, 0);
}

// Traduce le voci di un menu caricato da risorsa (ricorsivo sui sottomenu)
static void TranslateMenu(HMENU m) {
    if (!g_en || !m) return;
    int n = GetMenuItemCount(m);
    for (int i = 0; i < n; ++i) {
        wchar_t buf[256];
        MENUITEMINFOW mi{};
        mi.cbSize = sizeof(mi);
        mi.fMask = MIIM_STRING | MIIM_SUBMENU | MIIM_FTYPE;
        mi.dwTypeData = buf;
        mi.cch = 256;
        if (!GetMenuItemInfoW(m, (UINT)i, TRUE, &mi)) continue;
        if (!(mi.fType & MFT_SEPARATOR) && mi.cch) {
            const wchar_t* t = TR(buf);
            if (t != buf) {
                MENUITEMINFOW set{};
                set.cbSize = sizeof(set);
                set.fMask = MIIM_STRING;
                set.dwTypeData = const_cast<LPWSTR>(t);
                SetMenuItemInfoW(m, (UINT)i, TRUE, &set);
            }
        }
        if (mi.hSubMenu) TranslateMenu(mi.hSubMenu);
    }
}
