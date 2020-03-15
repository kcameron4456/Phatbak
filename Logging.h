#ifndef LOGGING_H
#define LOGGING_H

#include "Opts.h"

#include <stdio.h>
#include <string>
#include <stdarg.h>
#include <mutex>
#include <chrono>

using namespace std;

#ifdef DBGMSG
extern mutex DBG_Mutex;
#define DBG(...) {if (O.DebugPrint) { \
                  unsigned long nowns = chrono::duration_cast<chrono::nanoseconds> \
                                        (chrono::system_clock::now().time_since_epoch()).count(); \
                  DBG_Mutex.lock(); \
                  fprintf (stderr, "%llu: ", nowns % 100000000000000ULL); \
                  fprintf (stderr, __VA_ARGS__); \
                  fflush  (stderr); \
                  DBG_Mutex.unlock(); \
                 }}
#define DBGALLOC(...) DBG(__VA_ARGS__) // TBD: add option to turn these off
#define DBGCTOR       DBGALLOC("%s CTOR:%p\n", __PRETTY_FUNCTION__, this)
#define DBGDTOR       DBGALLOC("%s DTOR:%p\n", __PRETTY_FUNCTION__, this)
#else
#define DBG(...)
#define DBGALLOC(...)
#define DBGCTOR
#define DBGDTOR
#endif

// exception handling
#define PB_VARGS(a) va_list args; va_start(args,a)
class PB_Exception {
    public:
    string Message;
    string MsgType;
    void PrintMessage () {
        fprintf (stderr, "%s\n", Message.c_str());
    }
    void MakeMessage (int SrcLine, const char *SrcFile, const string &fmt, va_list args) {
        char LBuf [100];
        snprintf (LBuf, 99, "%d", SrcLine);
        string NewFmt = (string)SrcFile + ":" + LBuf + ": " + MsgType + fmt;

        char cmsg [4000];
        vsnprintf (cmsg, 999, NewFmt.c_str(), args);
        Message = cmsg;
    }
    void MakeMessage (int SrcLine, const char *SrcFile, const string &fmt, ...) {
        PB_VARGS(fmt);
        MakeMessage (SrcLine, SrcFile, fmt, args);
        va_end(args);
    }
    virtual void Print () {
        PrintMessage ();
    }
    virtual void Handle () {
        Print();
        exit (1);
    }
    PB_Exception () {
        Message = "PB_Exception - undefined exception type";
    }
    PB_Exception (int SrcLine, const char *SrcFile, const string &fmt, ...) {
        PB_VARGS(fmt);
        MakeMessage (SrcLine, SrcFile, fmt, args);
        va_end(args);
    }
};
class PB_ExceptionFMT : public PB_Exception {
    public:
    PB_ExceptionFMT (int SrcLine, const char *SrcFile, const string &fmt, ...) {
        MsgType = "PhatBak Format Error: ";
        PB_VARGS(fmt);
        MakeMessage (SrcLine, SrcFile, fmt, args);
        va_end(args);
    }
};
class PB_ExceptionIO : public PB_Exception {
    int ErrNo;
    public:
    void Print () {
        errno = ErrNo;
        perror (Message.c_str());
    }
    void Handle () {
        Print ();
        exit (ErrNo);
    }
    PB_ExceptionIO (int SrcLine, const char *SrcFile, const string &fmt, ...) {
        MsgType = "IO Error: ";
        ErrNo = errno;
        PB_VARGS(fmt);
        MakeMessage (SrcLine, SrcFile, fmt, args);
        va_end(args);
    }
};

// macros to include file name and line number in exception
#define THROW_PBEXCEPTION_GEN(TYPE, ...) throw PB_Exception##TYPE (__LINE__, __FILE__, __VA_ARGS__)
#define THROW_PBEXCEPTION_IO(...)        THROW_PBEXCEPTION_GEN (IO , __VA_ARGS__)
#define THROW_PBEXCEPTION_FMT(...)       THROW_PBEXCEPTION_GEN (FMT, __VA_ARGS__)
#define THROW_PBEXCEPTION(...)           THROW_PBEXCEPTION_GEN (   , __VA_ARGS__)

#endif // LOGGING_H
