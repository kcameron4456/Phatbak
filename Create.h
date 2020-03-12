#ifndef CREATE_H
#define CREATE_H

#include "LiveFile.h"
#include "RepoInfo.h"
#include "ArchiveCreate.h"

#include <string>
#include <vector>
#include <map>
using namespace std;

class Create {
    public:
    RepoInfo                  Repo;      // information about current repository
    ArchiveCreate             Arch;      // information about current repository
    vector <string>           FileList;  // ordered list of files to be backed up
    map    <string, LiveFile> FileMap;   // lookup file structs by name
    map <uint32_t, map <uint64_t, ArchFileCreate*>> Inodes; // archive info for each inode of each block device

     Create ();
    ~Create ();
    void DoCreate (const string &Dir);
};

#endif // CREATE_H
