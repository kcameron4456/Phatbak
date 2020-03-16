#ifndef TYPES_H
#define TYPES_H

#include <string>
using namespace std;

// these are mostly put here to break cicular header dependencies


typedef enum {
    CompType_NONE = 0,
    CompType_ZTSD,
    CompType_NULL,  // marks end of list
} eCompType;

#include "Hash.h"

typedef unsigned long long BlockIdxType;

class ChunkInfo {
    public:
    eCompType    CompType;
    BlockIdxType Idx;
    string       Hash;
    eHashType    HashType;
    ChunkInfo (eCompType comptype, BlockIdxType idx, const string& hash, eHashType hashtype) {
        CompType = comptype;
        Idx      = idx;
        Hash     = hash;
        HashType = hashtype;
    }
};

#endif // TYPES_H
