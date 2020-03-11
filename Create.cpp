#include "Create.h"
#include "Opts.h"
#include "Logging.h"

#include <string>
#include <vector>

Create::Create () {
    Repo.Init (O.RepoDirName);
    Arch.Init (&Repo, O.ArchDirName);
}

Create::~Create () {
}

void Create::DoCreate (const string &Name) {
    if (O.ShowFiles)
        printf ("%s\n", Name.c_str());

    // create local and archive file structures
    LiveFile LF (Name);
    ArchFile *AF = new ArchFile (&Arch);

    // remember info for each file
    FileList.push_back (Name);          // list of all files
    FileMap [Name] = LF;                // reference file info by name

    // if the device and inode has already been seen, process hard link
    bool KeepAF = false;
    if (uint32_t INode = LF.INode()) {
        uint32_t Dev = LF.Dev();
        if (Inodes      .find(Dev  ) != Inodes      .end() &&
            Inodes [Dev].find(INode) != Inodes [Dev].end()) {
            // this dev/inode combo has been seen before

            // create the link
            ArchFile *PrevAF = Inodes [Dev][INode];
            AF->CreateLink (LF, PrevAF);

            // that's all
            return;
        } else {
            // remember the first instance of this dev/inode combo
            Inodes [Dev][INode] = AF;
            KeepAF = true;
        }
    }

    // create the archived file
    AF->Create(LF);

    // create sub dirs/files
    for (auto Sub : LF.GetSubs())
        DoCreate (Sub);

    if (!KeepAF)
        delete AF;
}
