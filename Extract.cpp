#include "Extract.h"
#include "Utils.h"
#include "Logging.h"

#include <filesystem>
namespace fs = std::filesystem;

Extract::Extract () {
    Repo = new RepoInfo (O.RepoDirName);
    Arch = new ArchiveRead (Repo, O.ArchDirName, O);
}

Extract::~Extract () {
    delete Arch;
    delete Repo;
}

void Extract::DoExtract () {
    // extract into new directory
    if (fs::exists (O.ExtractTarget))
        THROW_PBEXCEPTION ("Can't extract into existing directory: %s\n", O.ExtractTarget.c_str());
    Utils::CreateDir (O.ExtractTarget);

    Arch->DoExtract();
}
