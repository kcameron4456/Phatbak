#include "Comp.h"
#include "Opts.h"
#include "Logging.h"

#include <stdlib.h>
#include <zstd.h>

eCompType Comp::CompNameToEnum (const string &Name) {
    for (int i = 0; i < CompType_NULL; i++) {
        if (Name == CompNames [i])
            return (eCompType) i;
    }
    THROW_PBEXCEPTION_FMT ("Unrecognized Compression Algorithm: " + Name);
}

eCompType Comp::CompFlag2CompType (char Flag) {
    return CompFlag2CompType (Flag, O);
}

eCompType Comp::CompFlag2CompType (char Flag, Opts &O) {
    switch (Flag) {
        case 'U' : return CompType_NONE;
        case 'C' : return O.CompType;
        default:
            THROW_PBEXCEPTION_FMT ("Unrecognized compression flag: %c", Flag);
    }
    return O.CompType;
}

char Comp::CompType2CompFlag (eCompType Type) {
    if (Type == CompType_NONE)
        return CompFlagUnComp;
    else
        return CompFlagComp;;
}

void Comp::Compress (const string &InStr, string &OutStr) {
    Compress (O.CompType, InStr, OutStr);
}

void Comp::Compress (eCompType CompType, const string &InStr, string &OutStr) {
    switch (CompType) {
        case CompType_ZTSD :
            Compress_ZSTD (InStr, OutStr);
            return;
        default:
            THROW_PBEXCEPTION ("Illegal compression type: %d", O.CompType);
    }
}

void Comp::DeCompress (char CompFlag, const string &InStr, string &OutStr) {
    DeCompress (CompFlag2CompType (CompFlag, O), InStr, OutStr);
}

void Comp::DeCompress (eCompType CompType, const string &InStr, string &OutStr) {
    switch (O.CompType) {
        case CompType_ZTSD :
            DeCompress_ZSTD (InStr, OutStr);
            break;
        default:
            THROW_PBEXCEPTION ("Illegal compression type: %d", O.CompType);
    }
}

void Comp::Compress_ZSTD (const string &InStr, string &OutStr) {
    OutStr.resize(ZSTD_COMPRESSBOUND(InStr.size()));
    auto CompSize = ZSTD_compress (OutStr.data(), OutStr.size(), InStr.data(), InStr.size(), O.CompLevel);
    if (ZSTD_isError(CompSize))
        THROW_PBEXCEPTION ("ZSTD Decompress error: %s\n", ZSTD_getErrorName (CompSize));
    OutStr.resize (CompSize);
}

void Comp::DeCompress_ZSTD (const string &InStr, string &OutStr) {
    ZSTD_DStream* ZSTD = ZSTD_createDStream();
    ZSTD_initDStream(ZSTD);

    unsigned AllocAmt   = O.ChunkSize;
    unsigned TotalAlloc = AllocAmt;
    unsigned TotalOut   = 0;

    ZSTD_inBuffer  in_buf = {InStr.data(), InStr.size(), 0};
    ZSTD_outBuffer out_buf;

    // loop until decompression is complete
    do {
        // extract one chunk of decompressed data
        OutStr.resize(TotalAlloc);
        out_buf = {OutStr.data(), TotalAlloc, TotalOut};
        int RVal = ZSTD_decompressStream(ZSTD, &out_buf, &in_buf);
        if (ZSTD_isError(RVal))
            THROW_PBEXCEPTION ("ZSTD Decompress error: %s\n", ZSTD_getErrorName (RVal));

        TotalOut     = out_buf.pos;
        TotalAlloc  += AllocAmt;
    } while (in_buf.pos < in_buf.size || out_buf.pos >= out_buf.size);

    OutStr.resize(TotalOut);

    ZSTD_freeDStream (ZSTD);
}
