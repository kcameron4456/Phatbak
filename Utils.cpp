#include "Utils.h"

vector <string> Utils::SplitStr (string Src, const string &Pat) {
    vector <string> Toks;
    while (Src.size()) {
        auto TokEnd = Src.find (Pat);
        string Tok = Src.substr (0, TokEnd);
        Toks.push_back(Tok);
        Src.erase (0, Tok.size() + Pat.size());
    }
    return Toks;
}
