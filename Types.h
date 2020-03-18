#ifndef TYPES_H
#define TYPES_H

#include <string>
#include <vector>
using namespace std;

// get easier names for commonly used types
// TBD: use these everywhere
typedef int32_t         i32;
typedef int64_t         i64;
typedef uint32_t        u32;
typedef uint64_t        u64;
typedef vector <string> vecstr;

// these are mostly put here to break circular header dependencies

typedef enum {
    CompType_NONE = 0,
    CompType_ZTSD,
    CompType_NULL,  // marks end of list
} eCompType;

#include "Hash.h"
#include "Comp.h"

class ChunkInfo {
    public:
    eCompType    CompType;
    i64          Idx;
    string       Hash;
    eHashType    HashType;
    ChunkInfo (eCompType comptype, i64 idx, const string& hash, eHashType hashtype) {
        CompType = comptype;
        Idx      = idx;
        Hash     = hash;
        HashType = hashtype;
    }
};

#endif // TYPES_H
