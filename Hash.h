#ifndef HASH_H
#define HASH_H

#include <mhash.h>
#include <string>
using namespace std;

typedef enum {
    HashType_MD5 = 0,
    HashType_CRC32,
    HashType_SHA1,
    HashType_SHA256,
    HashType_Null,  // denotes end of list
} eHashType;

static const char *  HashNames [] = {"MD5", "CRC32", "SHA1", "SHA256"};

static hashid       MHashTypes [] = {MHASH_MD5, MHASH_CRC32, MHASH_SHA1, MHASH_SHA256};

class Hash {
    MHASH Hasher;
    int HashSize;

    public:
    Hash (eHashType T);
    void   Update  (const char *Buf, int BufSize);
    string GetHash ();
};

eHashType HashNameToEnum (const string &Name);

#endif // HASH_H
