#include "RepoInfo.h"
#include "Logging.h"

#include <filesystem>
#include <string>
namespace fs = std::filesystem;

RepoInfo::RepoInfo (const string &name) {
    Name = name;
    if (!fs::is_directory (Name))
        THROW_PBEXCEPTION_FMT ("Repo dir (%s) not found", Name.c_str());

    // check for repo consistancy
    string RepoId = Name + "/" + PHATBAK_REPO_ID;
    if (!fs::exists (RepoId))
        // TBD: optionally create repo if it doesn't exist
        THROW_PBEXCEPTION_FMT ("Repo Indentifier (%s) not found", RepoId.c_str());

    // check for previous reference archive 
    LatestArchName = "";
    //don't allow reference archive for now
    //TBD: get rid of this
    #if (0)
    if (O.RefArchive == "") {
        string LatestArchLink = Name + "/LatestArchive";
        if (fs::exists (LatestArchLink + "/" PHATBAK_ARCH_ID))
            LatestArchName = fs::read_symlink (LatestArchLink);
    } else {
        string TryName = O.RefArchive;
        if (fs::exists (Name + "/" + TryName + "/" PHATBAK_ARCH_ID))
            LatestArchName = TryName;
        else
            THROW_PBEXCEPTION_FMT ("Reference archive (%s) doesn't exist", O.RefArchive.c_str());
    }
    #endif
}

void RepoInfo::Finish (const string &ArchName) {
    string LinkName = Name + "/LatestArchive";
    error_code ec;
    fs::remove                   (          LinkName, ec);
    fs::create_directory_symlink (ArchName, LinkName, ec);
    if (ec)
        THROW_PBEXCEPTION_IO ("Can't create symlink: %s", LinkName.c_str());
}
