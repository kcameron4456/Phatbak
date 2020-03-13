#ifndef TYPES_H
#define TYPES_H

#include "Hash.h"

#include <string>
using namespace std;

typedef uint64_t BlockIdxType;

class ChunkInfo {
    public:
    char         Comp;
    BlockIdxType Idx;
    string       Hash;
    eHashType    HashType;
    ChunkInfo (char comp, BlockIdxType idx, const string& hash, eHashType hashtype) {
        Comp     = comp;
        Idx      = idx;
        Hash     = hash;
        HashType = hashtype;
    }
};

#endif // TYPES_H
