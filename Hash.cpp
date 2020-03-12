#include "Hash.h"
#include "Logging.h"

#include <mhash.h>
#include <string>
using namespace std;

Hash::Hash (eHashType T) {
    if (T >= HashType_Null)
        THROW_PBEXCEPTION ("Unrecognized hash type: %d", T);
    Hasher = mhash_init (MHashTypes [T]);
    HashSize = mhash_get_block_size(MHashTypes [T]);
}

void Hash::Update (const char *Buf, int BufSize) {
    mhash (Hasher, Buf, BufSize);
}

string Hash::GetHash () {
    unsigned char *HashBin = (unsigned char *)mhash_end (Hasher);
    string HashHex;
    for (int i = 0; i < HashSize; i++) {
        char Hex [3];
        snprintf (Hex, 3, "%02x", HashBin[i]);
        HashHex += Hex;
    }
    return HashHex;
}

eHashType HashNameToEnum (const string &Name) {
    for (int i = 0; i < HashType_Null; i++) {
        if (Name == HashNames [i])
            return (eHashType) i;
    }
    THROW_PBEXCEPTION_FMT ("Unrecognized Hash Algorythm: " + Name);
}
