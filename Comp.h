#ifndef COMP_H
#define COMP_H

#include "Types.h"
#include "Opts.h"

#include <string>
using namespace std;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"

static const char CompFlagUnComp = 'U';  // in file list and finfo, signifies uncompressed block
static const char CompFlagComp   = 'C';  // in file list and finfo, signifies compressed block

static const char *CompNames [] = {"none", "zstd"};

class Opts; // needed by circular header dependecies

namespace Comp {
    eCompType CompFlag2CompType (char Flag, Opts &O);
    eCompType CompFlag2CompType (char Flag);
    char      CompType2CompFlag (eCompType Type);
    eCompType CompNameToEnum    (const string &Name);

    void        Compress   (                    const string &InStr, string &OutStr);
    void        Compress   (eCompType CompType, const string &InStr, string &OutStr);
    void      DeCompress   (eCompType CompType, const string &InStr, string &OutStr);
    void      DeCompress   (char      CompFlag, const string &InStr, string &OutStr);

    void        Compress_ZSTD (const string &InStr, string &OutStr);
    void      DeCompress_ZSTD (const string &InStr, string &OutStr);
};


#endif // COMP_H
