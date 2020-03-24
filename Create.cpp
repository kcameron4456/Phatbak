#include "Create.h"
#include "Opts.h"
#include "Logging.h"
#include "Utils.h"
#include "ThreadPool.h"

#include <string>
#include <vector>
#include <filesystem>
#include <iostream>
using namespace std;
namespace fs = std::filesystem;

Create::Create () {
    Repo = new RepoInfo   (O.RepoDirName);

    // see if we want to base the new archive to a previous one
    bool Rebase = O.Rebase || Repo->LatestArchName == "" || Repo->LatestArchName == O.ArchDirName;
    ArchBase = NULL;
    if (Rebase) {
        printf ("Creating new base archive: %s::%s\n", Repo->Name.c_str(), O.ArchDirName.c_str());
    } else {
        printf ("Creating archive: %s::%s using base archive: %s::%s\n",
                Repo->Name.c_str(), O.ArchDirName.c_str(), Repo->Name.c_str(), Repo->LatestArchName.c_str());

        ArchBase = new ArchiveBase (Repo, Repo->LatestArchName);
    }

    Arch = new ArchiveCreate (Repo, O.ArchDirName, ArchBase);
}

Create::~Create () {
    delete Arch;
    if (ArchBase)
        delete ArchBase;
    delete Repo;
}

void Create::DoCreate () {
    // do the archive creation
    for (auto Dir : O.FileArgs)
        DoCreate (Utils::CanonizeFileName (Dir));

    // wait for threads to complete
    ThreadPool.WaitIdle();

    Repo->Finish(O.ArchDirName);
}

void Create::DoCreate (const string &Name) {
    if (O.ShowFiles)
        cout << Name << endl;

    // create local and archive file structures
    LiveFile       *LF   = new LiveFile (Name);
    vecstr          Subs = LF->GetSubs();
    ArchFileCreate *AF   = new ArchFileCreate (Arch, LF);

    // if the device and inode has already been seen, process hard link
    bool KeepAF = false;
    if (uint32_t INode = LF->INode()) {
        uint32_t Dev = LF->Dev();
        if (Inodes.count (Dev) && Inodes [Dev].count(INode)) {
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
    for (auto &Sub : Subs)
        DoCreate (Sub);
}
