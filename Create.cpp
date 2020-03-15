#include "Create.h"
#include "Opts.h"
#include "Logging.h"

#include <string>
#include <vector>

Create::Create () {
    Repo = new RepoInfo   (O.RepoDirName);
    Arch = new ArchiveCreate (Repo, O.ArchDirName);
}

Create::~Create () {
    delete Arch;
    delete Repo;
}

void Create::DoCreate (const string &Name) {
    if (O.ShowFiles)
        cout << Name << endl;

    // create local and archive file structures
    LiveFile       *LF      = new LiveFile (Name);
    ArchFileCreate *AF      = new ArchFileCreate (Arch, LF);
    vector <string> SubDirs = LF->GetSubs();

    // if the device and inode has already been seen, process hard link
    bool KeepAF = false;
    if (uint32_t INode = LF->INode()) {
        uint32_t Dev = LF->Dev();
        if (Inodes      .find(  Dev) != Inodes      .end() &&
            Inodes [Dev].find(INode) != Inodes [Dev].end()) {
            // this dev/inode combo has been seen before

            // create the link
            ArchFileCreate *PrevAF = Inodes [Dev][INode];
            AF->CreateLink (PrevAF);

            // that's all
            return;
        } else {
            // remember the first instance of this dev/inode combo
            Inodes [Dev][INode] = AF;
            KeepAF = true;
        }
    }

    // create the archived file
    AF->Create(KeepAF);

    // create sub dirs/files
    for (auto Sub : SubDirs)
        DoCreate (Sub);
}
