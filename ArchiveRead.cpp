#include "ArchiveRead.h"
#include "Logging.h"

#include<string>
#include<fstream>

void ArchiveRead::Init (RepoInfo *repo, Opts &o) {
    Repo        = repo;
    O           = o;
    Name        = O.ArchDirName;
    ArchDirPath = Repo->Name + "/" + Name;
    ParseOptions ();
}

void ArchiveRead::DoExtract () {
    // open the file list
}

void ArchiveRead::ParseOptions () {
    // extract options from the archive file
    string OptsFileN = ArchDirPath + "/Options";
    ifstream OptsFile (OptsFileN);
    if (OptsFile.fail())
        THROW_PBEXCEPTION_IO ("Can't open %s for read", OptsFileN.c_str());
    string OptLine;
    while (getline (OptsFile, OptLine)) {
        // skip non-option lines
        if (OptLine.find ("=") == string::npos)
            continue;

        // get rid of leading whitespace
        auto NameBegin = OptLine.find_first_not_of (" \t");

        // option name is everything before next whitespace
        auto NameEnd   = OptLine.find_first_of (" \t", NameBegin);
        string OptName = OptLine.substr (NameBegin, NameEnd - NameBegin);

        // get option value
        auto ValBegin = OptLine.find_first_not_of (" \t=", NameEnd);
        string Val    = OptLine.substr (ValBegin);

        if (OptName == "BlockNumDigits" ) O.BlockNumDigits  = stoull(Val);
        if (OptName == "BlockNumModulus") O.BlockNumModulus = stoull(Val);
        if (OptName == "ChunkSize"      ) O.ChunkSize       = stoull(Val);
        if (OptName == "HashType"       ) O.HashType        = HashNameToEnum (Val) ;
    }

    O.Print();
    OptsFile.close();
}

