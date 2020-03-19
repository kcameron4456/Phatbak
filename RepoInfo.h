#ifndef REPOINFO_H
#define REPOINFO_H

#include <string>
using namespace std;

#define PHATBAK_REPO_ID "Is_PhatBak_Repo"
#define PHATBAK_ARCH_ID "Is_PhatBak_Archive"

class RepoInfo {
    public:
    string Name;
    string LatestArchName;

    RepoInfo (const string &name);
    void Finish (const string &ArchName);
};

#endif // REPOINFO_H
