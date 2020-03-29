#include "Utils.h"
#include "Logging.h"
using namespace Utils;

#include <iostream>
using namespace std;

int main () {
    try {
        vector <string> StrList = {"aa", "bb", "cc"};
        cout << "Joined 1=" << JoinStrs (StrList) << endl;
        cout << "Joined 2=" << JoinStrs (StrList, "*") << endl;
        cout << "Joined 3=" << JoinStrs (StrList, "/") << endl;

        vector <string> Paths = {"/", "/ss/..", "/ss/../..", "ss///../tt/..//./" , "..", "../tartar"};
        for (auto Path : Paths) {
            string CPath = CanonizeFileName (Path);
            cout << "Path:" << Path << ": CPath:" << CPath << ":" << endl;
        }

        printf ("sizeof(unsigned) = %lu\n", sizeof (unsigned));

        //CreateDir ("zzz");
        //CreateDir ("zzz");
        //CreateDir ("zzzz/bbb/zzz", 1);
        //CreateDir ("zzzz/bbbb/zzz");
    }

    // handle exceptions
    catch (const char *msg) {
        fprintf (stderr, "%s\n", msg);
        return 1;
    }
    catch (PB_Exception &PBE) {
        PBE.Print();
    }
}
