#include "RepoInfo.h"
#include "Logging.h"
#include "Utils.h"

#include <filesystem>
#include <string>
namespace fs = std::filesystem;

RepoInfo::RepoInfo (const string &name) {
    Name = name;
    if (!fs::is_directory (Name))
        ERROR ("Repo dir (%s) not found\n", Name.c_str());

    // check for repo consistancy
    string RepoId = Name + "/" + PHATBAK_REPO_ID;
    if (!fs::exists (RepoId))
        // TBD: optionally create repo if it doesn't exist
        ERROR ("Repo Indentifier (%s) not found\n", RepoId.c_str());

    // check for previous base archive 
    LatestArchName = "";
    if (!O.Rebase) {
        vecstr SubDirs, SubFiles;
        Utils::SlurpDir (Name, SubDirs, SubFiles);

        // find most recent standard archive (i.e. name is time in standaridized format)
        for (auto SubDir : SubDirs) {
            if (!fs::exists (SubDir + "/" + PHATBAK_ARCH_ID))
                continue;
            static const string Pattern = "XXXX_XX_XX_XXXX_XX";
            if (SubDir.size() != Pattern.size())
                continue;
            string TempSubDir = SubDir;
            for (size_t i = 0; i < TempSubDir.size(); i++)
                if (TempSubDir[i] >= '0' && TempSubDir[i] <= '9')
                    TempSubDir[i] = 'X';
            if (TempSubDir != Pattern)
                continue;
            if (SubDir > LatestArchName)
                LatestArchName = SubDir;
        }
    }
}

void RepoInfo::Finish (const string &ArchName) {
    //string LinkName = Name + "/LatestArchive";
    //error_code ec;
    //fs::remove                   (          LinkName, ec);
    //fs::create_directory_symlink (ArchName, LinkName, ec);
    //if (ec)
    //    THROW_PBEXCEPTION_IO ("Can't create symlink: %s", LinkName.c_str());
}

void RepoInfo::DoList () {
    vecstr SubDirs, SubFiles;
    Utils::SlurpDir (Name, SubDirs, SubFiles);
    for (auto SubDir : SubDirs)
        if (fs::exists (Name + "/" + SubDir + "/" + PHATBAK_ARCH_ID))
            printf ("%s::%s\n", Name.c_str(), SubDir.c_str());
}
