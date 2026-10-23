// Test del parser JSON (portabile): g++ -std=c++17 -fsanitize=address,undefined tests/json_test.cpp -o json_test && ./json_test
#include "../src/json.h"
#include <cstdio>
#include <string>
static int fails = 0;
static void check(bool c, const char* what) { if (!c) { ++fails; printf("FAIL %s\n", what); } else printf("ok   %s\n", what); }
static bool validUtf8(const std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = s[i];
        int n = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
        if (n < 0 || i + n >= s.size() + (n ? 0 : 1)) { if (n) return false; }
        if (n < 0) return false;
        for (int k = 1; k <= n; ++k) if (i + k >= s.size() || ((unsigned char)s[i + k] >> 6) != 2) return false;
        if (n == 2) { unsigned cp = ((c & 15) << 12) | ((s[i+1] & 63) << 6) | (s[i+2] & 63); if (cp >= 0xD800 && cp <= 0xDFFF) return false; }
        i += n + 1;
    }
    return true;
}
int main() {
    JVal v;
    std::string a = R"({"title":"Ciao è 😀 fine","n":-1.5e3,"arr":[1,true,null,"x"],"o":{"k":"v"}})";
    check(JParser(a.data(), a.size()).parse(v) && v.t == JVal::Obj, "oggetto valido");
    check(v.str("title") == "Ciao \xc3\xa8 \xf0\x9f\x98\x80 fine", "accenti e emoji (coppia surrogata)");
    check(v.num("n") == -1500, "numero con esponente");
    check(v.get("arr") && v.get("arr")->items.size() == 4, "array");
    check(v.get("o") && v.get("o")->str("k") == "v", "oggetto annidato");
    JVal w; std::string b = R"({"t":"a\ud83db","u":"\ude00x","z":"\ud83dA"})";
    check(JParser(b.data(), b.size()).parse(w), "surrogati isolati: parse ok");
    check(validUtf8(w.str("t")) && validUtf8(w.str("u")) && validUtf8(w.str("z")), "surrogati isolati: UTF-8 valido");
    check(w.str("z") == "\xef\xbf\xbd" "A", "alta + carattere normale: segnaposto + A");
    std::string c("{\"x\":1\0}", 8);
    JVal x; check(!JParser(c.data(), c.size()).parse(x) || x.num("x") == 1, "byte nullo non mangiato come numero");
    std::string d = R"({"a":[1,2,)";
    JVal y; check(!JParser(d.data(), d.size()).parse(y), "input troncato rifiutato");
    std::string deep(300, '['); deep += std::string(300, ']');
    JVal z; check(!JParser(deep.data(), deep.size()).parse(z), "annidamento eccessivo rifiutato");
    std::string e = R"({"v":NaN,"w":Infinity})";
    JVal q; check(JParser(e.data(), e.size()).parse(q) && !q.has("v"), "NaN/Infinity come null");
    printf("%s (%d errori)\n", fails ? "FALLITO" : "TUTTO OK", fails);
    return fails != 0;
}
