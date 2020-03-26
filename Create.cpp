#include "Create.h"
#include "Opts.h"
#include "Logging.h"
#include "Utils.h"
#include "ThreadPool.h"
using namespace Utils;

#include <string>
#include <vector>
#include <filesystem>
#include <iostream>
#include <algorithm>
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
    // find unique roots of file args so we can archive them with
    // proper attributes
    map <string, bool> Roots;
    for (auto &Dir : O.FileArgs) {
        string CanDir = CanonizeFileName (Dir);
        vecstr Parts = SplitStr (CanDir, "/");
        Parts.pop_back();
        vecstr Root;
        for (auto &Part : Parts) {
            if (!Part.size())
                continue;
            Root.push_back(Part);
            Roots [string ("/") + JoinStrs (Root, "/")] = 1;
        }
    }

    // sort the roots to archive them in top down order
    vecstr Sorted;
    for (auto Itr : Roots)
        Sorted.push_back (Itr.first);
    sort (Sorted.begin(), Sorted.end());

    // do the archive creation
    for (auto Dir : Sorted)
        DoCreate (Dir, 0);

    // do the archive creation
    for (auto Dir : O.FileArgs)
        DoCreate (CanonizeFileName(Dir));

    // wait for threads to complete
    ThreadPool.WaitIdle();

    Repo->Finish(O.ArchDirName);
}

void Create::DoCreate (const string &Name, bool Recurse) {
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
    if (Recurse)
        for (auto &Sub : Subs)
            DoCreate (Sub);
}
