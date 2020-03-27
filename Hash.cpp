#include "Hash.h"
#include "Logging.h"

#include <mhash.h>
#include <string>
using namespace std;

Hash::Hash (eHashType T) {
    if (T >= HashType_Null)
        THROW_PBEXCEPTION ("Unrecognized hash type: %d", T);
    Hasher = mhash_init (MHashTypes [T]);
    assert (Hasher != NULL);
    HashSize = mhash_get_block_size(MHashTypes [T]);
}

Hash::~Hash () {
}

void Hash::Update (const char *Buf, int BufSize) {
    mhash (Hasher, Buf, BufSize);
}

string Hash::GetHash () {
    unsigned char *HashBin = (unsigned char *)mhash_end_m (Hasher, (void * (*)(unsigned int)) malloc);
    string HashHex;
    // TBD: do more than one byte at a time
    for (int i = 0; i < HashSize; i++) {
        char Hex [3];
        snprintf (Hex, 3, "%02x", HashBin[i]);
        HashHex.append(Hex);
    }
    free (HashBin);
    return HashHex;
}

string Hash::HashStr (const string &Str) {
    Update (Str.data(), Str.size());
    return GetHash();
}

eHashType HashNameToEnum (const string &Name) {
    for (int i = 0; i < HashType_Null; i++) {
        if (Name == HashNames [i])
            return (eHashType) i;
    }
    THROW_PBEXCEPTION_FMT ("Unrecognized Hash Algorythm: " + Name);
}

string HashStr (eHashType T, const string &Str) {
    Hash Hasher(T);
    return Hasher.HashStr (Str);
}
