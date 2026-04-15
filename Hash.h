#ifndef HASH_H
#define HASH_H

typedef bool _Bool;  // needed for clang mhash compile
#include <mhash.h>

#include <string>
using namespace std;

typedef enum {
    HashType_MD5 = 0,
    HashType_CRC32,
    HashType_SHA1,
    HashType_SHA256,
    HashType_SHA512,
    HashType_Null,  // denotes end of list
} eHashType;

// map option names to enum
constexpr const char *  HashNames  [] = {"MD5"    , "CRC32"    , "SHA1"    , "SHA256"    , "SHA512"    };
constexpr const hashid  MHashTypes [] = {MHASH_MD5, MHASH_CRC32, MHASH_SHA1, MHASH_SHA256, MHASH_SHA512};

class Hash {
    MHASH Hasher;
    int HashSize;

    public:
    Hash (eHashType T);
    ~Hash ();
    void   Update  (const char *Buf, int BufSize);
    string GetHash ();
    string HashStr (const string &Str);
};

eHashType HashNameToEnum (const string &Name);

string HashStr (eHashType T, const string &Str);

#endif // HASH_H
