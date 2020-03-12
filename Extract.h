#ifndef EXTRACT_H
#define EXTRACT_H

#include "Opts.h"
#include "RepoInfo.h"
#include "ArchiveRead.h"

class Extract {
    Opts                         O;  // local options so portions can be overridden by file contents
    RepoInfo                  Repo;  // information about current repository
    ArchiveRead               Arch;  // information about current repository

    public:
    Extract (Opts &o);
    void DoExtract ();
};

#endif // EXTRACT_H
