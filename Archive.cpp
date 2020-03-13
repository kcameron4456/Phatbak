#include "Archive.h"
#include "Logging.h"
#include "Utils.h"
using namespace Utils;

#include <string>
#include <fstream>
#include <filesystem>
namespace fs = std::filesystem;

Archive::Archive(RepoInfo *repo, const string &name) {
    Repo         = repo;
    Name         = name;
    ArchDirPath  = Repo->Name + "/" + Name;
    ListPath     = ArchDirPath + "/List";
    LogPath      = ArchDirPath + "/PhatBak.log";
    OptionsPath  = ArchDirPath + "/Options";
    FinfoDirPath = ArchDirPath + "/FInfo";
    ChunkDirPath = ArchDirPath + "/Chunks";
    ExtraDirPath = ArchDirPath + "/Extra";
}

Archive::~Archive() {
    LogFile .close();
    ListFile.close();
}

ArchiveRead::ArchiveRead (RepoInfo *repo, const string &name, Opts &o) : Archive (repo, name) {
    O = o;
    ParseOptions ();

    FInfoBlocks = new BlockList (FinfoDirPath, O);
    ChunkBlocks = new BlockList (ChunkDirPath, O);
}

ArchiveRead::~ArchiveRead() {
}

ArchiveCreate::ArchiveCreate (RepoInfo *repo, const string &name) : Archive (repo, name) {
    Repo = repo;
    Name = name;

    // create archive dir
    CreateDir (ArchDirPath);

    // create the log file
    LogFile = OpenWriteStream (LogPath);
    LogFile << "Backup Started At: " << asctime(localtime(&O.StartTime)) << endl;

    // create Options file
    fstream OptFile = OpenWriteStream (OptionsPath);
    O.Print (OptFile);
    OptFile.close();

    // create new archive subdirs
    for (auto SubDir : {ChunkDirPath, FinfoDirPath, ExtraDirPath})
        CreateDir (SubDir);

    // initialize block allocators
    FInfoBlocks = new BlockList (FinfoDirPath, O);
    ChunkBlocks = new BlockList (ChunkDirPath, O);

    // start the file list
    ListFile = OpenWriteStream (ListPath);
}

ArchiveCreate::~ArchiveCreate () {
    time_t EndTime = time(NULL);
    LogFile << "Backup Ended At: " << ctime(&EndTime) << endl;

    int Secs = difftime (EndTime, O.StartTime);
    LogFile << "Elasped Time: " << Secs << " seconds\n";

    LogFile .close();
    ListFile.close();

    delete ChunkBlocks;
    delete FInfoBlocks;
}

void ArchiveRead::ParseOptions () {
    // extract options from the archive file
    fstream OptsFile = OpenReadStream (OptionsPath);
    string OptLine;
    while (getline (OptsFile, OptLine)) {
        // split into name/value pairs
        vector <string> Toks = SplitStr (OptLine, "=");

        // skip non-option lines
        if (Toks.size() != 2)
            continue;

        // clean up tokens
        for (auto &Tok : Toks)
            TrimStr (&Tok);

        // rename tokens
        string &OptName = Toks[0];
        string &OptVal  = Toks[1];

             if (OptName == "BlockNumDigits" ) O.BlockNumDigits  = stoull         (OptVal);
        else if (OptName == "BlockNumModulus") O.BlockNumModulus = stoull         (OptVal);
        else if (OptName == "ChunkSize"      ) O.ChunkSize       = stoull         (OptVal);
        else if (OptName == "HashType"       ) O.HashType        = HashNameToEnum (OptVal);
    }

    O.Print();
    OptsFile.close();
}

void ArchiveRead::DoExtract () {
    // open the file list
    string ListFileN = ArchDirPath + "/List";
    ifstream ListFile (ListPath);
    if (ListFile.fail())
        THROW_PBEXCEPTION_IO ("ArchiveRead::DoExtract - Can't open %s for read", ListPath.c_str());

    // parse and extract all the entries in the list
    string FileLine;
    uint64_t LineNo = 0;
    while (getline (ListFile, FileLine)) {
        LineNo ++;
        ArchFileRead *AF = new ArchFileRead (this, FileLine, LineNo);
        delete AF;
    }

    ListFile.close();
}

ArchFile::ArchFile (Archive *arch) {
    Arch = arch;
}

ArchFile::~ArchFile () {
}

ArchFileRead::ArchFileRead (ArchiveRead *arch, const string &ListEntry, uint64_t LineNo) : ArchFile (arch) {
    // parse file list entry
    // separate filename from attributes
    vector <string> FirstCut = SplitStr (ListEntry, " /../ ");
    if (FirstCut.size() != 2)
        THROW_PBEXCEPTION_FMT ("%s:%llu has bad format", Arch->ListPath.c_str(), LineNo);
    Name = FirstCut[0];

    // separate fields of rhs
    vector <string> RHSToks = SplitStr (FirstCut[1], " ");
    if (RHSToks.size() != 3)
        THROW_PBEXCEPTION_FMT ("%s:%llu has bad format", Arch->ListPath.c_str(), LineNo);
    InfoBlkNum  = stoull (RHSToks[0]);
    InfoBlkComp = RHSToks[1][0];
    InfoBlkHash = RHSToks[2];

    // extract information from the FInfo block
    string FInfoPacked;
    Arch->FInfoBlocks->SlurpBlock (InfoBlkNum, FInfoPacked);

    // TBD: handle decompress

    // parse finfo
    vector <string> FInfoLines = SplitStr (FInfoPacked, "\n");
    for (auto Line : FInfoLines) {
        if (Line.size() < 3 || Line[1] != '-')
            THROW_PBEXCEPTION_FMT ("Illegal FInfo format: %s", Line.c_str());
        char RecType = Line[0];
        Line.erase (0,2);
        switch (RecType) {
            case 'H' :
                Stats = Line; break;
                // {   vector <string> Fields = SplitStr (Line, " ");
                //    for (auto Field : Fields) {
                //        vector <string> Two = SplitStr (Field, ":");
                //        if (Two.size() != 2)
                //            THROW_PBEXCEPTION_FMT ("Illegal header field : %s", Field.c_str());
                //        string &Name = Two[0];
                //        string &Val  = Two[1];
                //        if (Name == "mode") 

                //    }
                //    break;
                //}
            case 'L' :
                LinkTarget = Line; break;

            case 'U' :
            case 'C' : {
                vector <string> Parts = SplitStr (Line, " ");
                DataChnks.emplace_back (RecType, stoull (Parts[0].c_str()), Parts[1]);
                break;
                }

            default :
                THROW_PBEXCEPTION_FMT ("Unrecognized FInfo record type '%c'", RecType);
        };
    }
}

ArchFileRead::~ArchFileRead () {
}

void ArchiveCreate::PushFileList (const string &Fname, BlockIdxType Block, char Comp, const string &Hash) {
    string FileEntry = Fname + " /../ " + to_string(Block) + " " + Comp + " " + Hash + "\n";
    ListFile << FileEntry;
}

ArchFileCreate::ArchFileCreate (ArchiveCreate *arch) {
    ArchCreate = arch;

    Mtx.lock();
}

void ArchFileCreate::Create (LiveFile &LF) {
    Name  = LF.Name;
    Stats = LF.MakeInfoHeader ();

    // Create Finfo file contents
    string FInfo = "H-" + Stats + "\n";

    // For files, create chunks and FInfo entries
    if (LF.IsFile()) {
        char Chunk [O.ChunkSize];
        LF.OpenRead();
        while (int RdSize = LF.ReadChunk (Chunk)) {
            // write the chunk to the archive
            BlockIdxType ChnkIdx = ArchCreate->ChunkBlocks->Alloc();
            FILE *ChunkF = ArchCreate->ChunkBlocks->OpenBlockFile (ChnkIdx, "wb");
            if (fwrite (Chunk, RdSize, 1, ChunkF) != 1)
                THROW_PBEXCEPTION_IO ("Error writing to Chunk block file: " + ArchCreate->ChunkBlocks->Idx2FileName(ChnkIdx));
            fclose (ChunkF);

            // compute hash
            Hash Hasher (O.HashType);
            Hasher.Update (Chunk, RdSize);
            string HashHex = Hasher.GetHash();

            // add chunk to finfo
            FInfo += "U-" + to_string (ChnkIdx) + " " + HashHex + "\n";
        }
        LF.Close();
    } else if (LF.IsSLink()) {
        FInfo += "L-" + LF.Target + "\n";
    }

    // Create the file info block in the archive
    InfoBlkNum = ArchCreate->FInfoBlocks->Alloc();
    FILE *FInfoF = ArchCreate->FInfoBlocks->OpenBlockFile (InfoBlkNum, "wb");
    if (fwrite (FInfo.c_str(), FInfo.size(), 1, FInfoF) != 1)
        THROW_PBEXCEPTION_IO ("Error writing to FInfo block file: " + ArchCreate->FInfoBlocks->Idx2FileName(InfoBlkNum));
    fclose (FInfoF);

    // get the finfo block hash
    Hash Hasher (O.HashType);
    Hasher.Update (FInfo.c_str(), FInfo.size());
    InfoBlkHash = Hasher.GetHash();

    // TBD: compress
    InfoBlkComp = 'U';

    // update archive file list
    ArchCreate->PushFileList (Name, InfoBlkNum, InfoBlkComp, InfoBlkHash);

    // flag completion
    Mtx.unlock();
}

// link to previously archived file
void ArchFileCreate::CreateLink (LiveFile &LF, ArchFileCreate *Prev) {
    Name  = LF.Name;

    // wait for processing of original file to complete
    Prev->Mtx.lock();

    ArchCreate->PushFileList (Name, Prev->InfoBlkNum, Prev->InfoBlkComp, Prev->InfoBlkHash);

    // release prev file
    Prev->Mtx.unlock();

    // release this file
    Mtx.unlock();
}
