#include "Extract.h"
#include "RepoInfo.h"

Extract::Extract (Opts &o) {
    O = o;
    Repo.Init (O.RepoDirName);
    Arch.Init (&Repo, O);
    //Init ();
}

void Extract::DoExtract () {
    Arch.DoExtract();
}
