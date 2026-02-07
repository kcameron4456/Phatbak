#include "RepoInfo.h"
#include "Logging.h"
#include "Utils.h"
#include "Archive.h"

#include <filesystem>
#include <string>
namespace fs = std::filesystem;

RepoInfo::RepoInfo (const string &name) {
    Name = Utils::CanonizeFileNameNoCWD (name);
    if (!fs::is_directory (Name)) {
        if (O.Operation == O.DoInit) {
            Utils::CreateDir (Name);
        } else {
            ERROR ("Repo dir (%s) not found\n", Name.c_str());
        }
    }

    // check for repo consistancy
    string RepoId = Name + "/" + PHATBAK_REPO_ID;
    if (!fs::exists (RepoId)) {
        if (O.Operation == O.DoInit) {
            Utils::Touch (RepoId);
        } else {
            ERROR ("Repo Indentifier (%s) not found\n", RepoId.c_str());
        }
    }

    // by default, use the newest existing archive as the backup base
    LatestArchName = "";
    string LatestTime;
    if (!O.Rebase) {
        vecstr SubDirs, SubFiles;
        Utils::SlurpDir (Name, SubDirs, SubFiles);

        for (auto SubDir : SubDirs) {
            // only consider valid archives
            if (  !fs::exists (Name + "/" + SubDir + "/" + PHATBAK_ARCH_ID)
               || !fs::exists (Name + "/" + SubDir + "/" + PHATBAK_ARCH_FINISHED)
               )
                continue;

            // extract creation time
            ArchiveRead AR (this, SubDir);
            auto ArchOpts = AR.GetOptions();
            if (ArchOpts.StartTimeTxt > LatestTime) {
                LatestArchName = SubDir;
                LatestTime     = ArchOpts.StartTimeTxt;
            }
        }
    }
}

void RepoInfo::Finish (const string &ArchName) {
}

void RepoInfo::DoList () {
    vecstr SubDirs, SubFiles;
    Utils::SlurpDir (Name, SubDirs, SubFiles);
    vecstr Archs;
    for (auto SubDir : SubDirs)
        if (fs::exists (Name + "/" + SubDir + "/" + PHATBAK_ARCH_ID))
            Archs.push_back (Name + "::" + SubDir);
    sort (Archs.begin(), Archs.end());
    for (auto &Arch : Archs)
        cout << Arch << endl;
}
