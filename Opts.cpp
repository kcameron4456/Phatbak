#include "Opts.h"
#include "Utils.h"
#include "Logging.h"

#include <grp.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sstream>
#include <filesystem>
using namespace std;
namespace fs  = filesystem;

// global options declaration
Opts O;

// default version - overriden in Makefile from git release tag
#ifndef VERSION_MAJOR
    #define VERSION_MAJOR 0
#endif
#ifndef VERSION_MINOR
    #define VERSION_MINOR 6
#endif

static void PrintHelp(int rval=0) {
    fprintf (stderr, "%s\n", R"(
Usage: PhatBak <Operation> [Options] Repo[::Archive] [file/dir list]
use "man PhatBak" for details
)");

    exit (rval);
}

static void ArgError (const char *arg, const char *nxtarg = NULL) {
    const char *a = nxtarg ? nxtarg : "";
    fprintf (stderr, "ERROR: Illegal Argument: %s %s\n", arg, a);
    PrintHelp(1);
}
static void ArgError (const string arg, const string nxtarg = "") {
    ArgError (arg.c_str(), nxtarg.c_str());
}

void PrintOpHelp () {
    fprintf (stderr, "ERROR: Exactly one flag of type {c -c x -x t -t} must be given\n");
    PrintHelp(1);
}

static void PrintVersion() {
    printf ("Version:  %d.%d\n", VERSION_MAJOR, VERSION_MINOR);
    exit (0);
}

// macro to skip to the next argument
#define NXTARG {                      \
    argidx++;                         \
    if (argidx >= CmdArgsList.size()) \
        ArgError (arg);               \
    arg = CmdArgsList[argidx];        \
}

// parse command line options
#define TESTOP {if (Operation != DoUndef) PrintOpHelp();}
void Opts::ParseCmdLine (const int argc, const char *argv[]) {
    // apply defaults
    Operation       = DoUndef;
    ShowFiles       = 0;
    NumThreads      = 100;
    CompType        = CompType_ZTSD;
    CompLevel       = 2;
    ChunkSize       = 1 << 18;
    HashType        = HashType_MD5;
    ExtractTarget   = "PhatBakExtract";
    DebugPrint      = 0;
    BlockNumModulus = 100;
    Rebase          = false;
    CWD             = fs::canonical(fs::current_path()); // resolves symlinks

    // copy command command line to c++ style vector
    vecstr CmdArgsList;
    for (int argidx=0; argidx < argc; argidx++)
        CmdArgsList.push_back ((string)argv[argidx]);
    CmdLine = Utils::JoinStrs (CmdArgsList);

    // Macros to assist argument parsing
    #define PARSE_MinusVal(Name, ValFmt, ValPtr, Action) { \
        if (arg == Name) {                                 \
            NXTARG                                         \
            if (sscanf (arg.c_str(), ValFmt, ValPtr) != 1) \
                ArgError (Name, arg);                      \
            Action                                         \
            continue;                                      \
        }                                                  \
    }
    #define PARSE_MinusStr(Name, ValNam, Action) { \
        if (arg == Name) { \
            NXTARG         \
            ValNam = arg;  \
            Action         \
            continue;      \
        }                  \
    }
    #define PARSE_MinusFlg(Name, ValNam, Action) { \
        if (arg == Name) {  \
            ValNam = 1;     \
            Action          \
            continue;       \
        }                   \
    }

    // process all of the "-" and "--" args
    // remaining args are operation and operation args
    vecstr OpArgList;
    for (size_t argidx = 1; argidx < CmdArgsList.size(); argidx++) {
        string arg = CmdArgsList [argidx];

        // save non-minus args
        if (arg[0] != '-') {
            OpArgList.push_back (arg);
            continue;
        }

        bool   TmpBool;
        static_cast<void>(TmpBool);  // prevent unused variable error
        string TmpStr;
        PARSE_MinusFlg ("-v"                , ShowFiles  , )
        PARSE_MinusFlg ("-D"                , ArchDiag   , )
        PARSE_MinusVal ("-T"                , "%d"       , &NumThreads,)
        PARSE_MinusStr ("--CompType"        , TmpStr     , CompType = Comp::CompNameToEnum(TmpStr);)
        PARSE_MinusVal ("--CompLevel"       , "%d"       , &CompLevel,)
        PARSE_MinusStr ("--HashType"        , TmpStr     , HashType = HashNameToEnum(TmpStr);)
        PARSE_MinusVal ("--ChunkSize"       , "%d"       , &ChunkSize,)
        PARSE_MinusVal ("--BlockNumModulus" , "%d"       , &BlockNumModulus,)
        PARSE_MinusFlg ("--Rebase"          , Rebase     , )
        PARSE_MinusStr ("--BaseArchive"     , BaseArchive, )
        PARSE_MinusFlg ("-h"                , TmpBool    , PrintHelp();)
        PARSE_MinusFlg ("-help"             , TmpBool    , PrintHelp();)
        PARSE_MinusFlg ("--help"            , TmpBool    , PrintHelp();)
        PARSE_MinusFlg ("-version"          , TmpBool    , PrintVersion();)
        PARSE_MinusFlg ("--version"         , TmpBool    , PrintVersion();)
        PARSE_MinusFlg ("--debug"           , DebugPrint , )

        ArgError(arg);
    }

    // first remaining arg is operation
    if (!OpArgList.size())
        ERROR ("No operation given\n");
    string OpArg = OpArgList [0];
    OpArgList.erase (OpArgList.begin());
         if (OpText (DoInit      ) == OpArg) Operation = DoInit      ;
    else if (OpText (DoCreate    ) == OpArg) Operation = DoCreate    ;
    else if (OpText (DoExtract   ) == OpArg) Operation = DoExtract   ;
    else if (OpText (DoTest      ) == OpArg) Operation = DoTest      ;
    else if (OpText (DoCompare   ) == OpArg) Operation = DoCompare   ;
    else if (OpText (DoList      ) == OpArg) Operation = DoList      ;
    else if (OpText (DoShowLatest) == OpArg) Operation = DoShowLatest;
    if (Operation == DoUndef)
        THROW_PBEXCEPTION ("Unrecognized Operation: %s", OpArg.c_str());

    // next arg is the repo/archive name
    if (!OpArgList.size())
        ERROR ("No repo name given\n");
    string RepoArchName = OpArgList [0];
    OpArgList.erase (OpArgList.begin());
    vector <string> Parts = Utils::SplitStr (RepoArchName, "::");
    if (Parts.size() > 2)
        PrintHelp(1);
    RepoDirName = Parts[0];
    if (Parts.size() == 2)
        ArchDirName = Parts[1];

    // remaining args are file/dir names for create or extract
    // default to cwd for create
    if (!OpArgList.size() && Operation == DoCreate)
        OpArgList.push_back(".");

    // normalize file args
    FileArgs.clear();
    for (auto &OpArg : OpArgList)
        FileArgs.push_back (fs::path(OpArg).lexically_normal ());

    // work around stdlib not removing trailing dir separator
    for (auto &FileArg : FileArgs) {
        while (FileArg.size() > 1 && (FileArg[FileArg.size()-1]) == fs::path::preferred_separator) {
            FileArg.resize(FileArg.size()-1);
        }
    }

    // make sure all the files/dirs exist
    for (auto &FileArg : FileArgs)
        if (!fs::exists (FileArg))
            THROW_PBEXCEPTION ("File/Dir does not exist: %s", FileArg.c_str());
}

Opts::Opts () {
    VersionMajor = VERSION_MAJOR;
    VersionMinor = VERSION_MINOR;
    StartTime    = Utils::TimeNowNs ();
    StartTimeTxt = Utils::NsToText (StartTime);
}

void Opts::Print (fstream &F) {
    F << OptsString ();
}

void Opts::Print (ostream &F) {
    F << OptsString ();
}

string Opts::OptsString () {
    stringstream F;
    F << "Options:\n";
    F << "   CmdLine         = " << CmdLine                         << endl;
    F << "   StartTime       = " << StartTimeTxt                    << endl;
    F << "   Operation       = " << OpText()                        << endl;
    F << "   FileArgs        = " << Utils::JoinStrs (FileArgs, " ") << endl;
    F << "   CWD             = " << CWD                             << endl;
    F << "   RepoDirName     = " << RepoDirName                     << endl;
    F << "   ArchDirName     = " << ArchDirName                     << endl;
    F << "   Rebase          = " << Rebase                          << endl;
    F << "   BaseArchive     = " << BaseArchive                     << endl;
    F << "   BlockNumModulus = " << BlockNumModulus                 << endl;
    F << "   ChunkSize       = " << ChunkSize                       << endl;
    F << "   HashType        = " << HashNames[HashType]             << endl;
    F << "   CompType        = " << CompNames[CompType]             << endl;
    F << "   CompLevel       = " << CompLevel                       << endl;
    F << "   ShowFiles       = " << ShowFiles                       << endl;
    F << "   DebugPrint      = " << DebugPrint                      << endl;
    return F.str();
}
