#ifndef CREATE_H
#define CREATE_H

#include "LiveFile.h"
#include "RepoInfo.h"
#include "Archive.h"

#include <string>
#include <vector>
#include <map>
using namespace std;

class Create {
    public:
    RepoInfo       *Repo;      // information about current repository
    ArchiveCreate  *Arch;      // information about current archive
    ArchiveBase    *ArchBase;  // information about base archive
    map <uint32_t, map <uint64_t, ArchFileCreate*>> Inodes; // archive info for each inode of each block device

     Create ();
    ~Create ();
    void DoCreate ();
    void DoCreate (const string &Dir);
};

#endif // CREATE_H
