#ifndef THREADPOOL_H
#define THREADPOOL_H

#include "Logging.h"
#include "Archive.h"

#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
using namespace std;

extern volatile int StopThreads;

class BusyLock {
    bool               Busy;
    mutex              Mtx;
    condition_variable CV;

    public:
    BusyLock () {
        Busy = false;
    }
    void WaitIdle () {
        unique_lock<mutex> lock(Mtx);
        CV.wait (lock, [this]{return !Busy || StopThreads;});
    }
    void WaitIdleAndPost () {
        unique_lock<mutex> lock(Mtx);
        CV.wait (lock, [this]{return !Busy || StopThreads;});
        Busy = 1;
    }
    void WaitBusy () {
        unique_lock<mutex> lock(Mtx);
        CV.wait (lock, [this]{return Busy || StopThreads;});
    }
    void WaitBusyAndPost () {
        unique_lock<mutex> lock(Mtx);
        CV.wait (lock, [this]{return Busy || StopThreads;});
        Busy = 0;
    }
    void Notify () {
        CV.notify_all();
    }
    void Post (bool B) {
        unique_lock<mutex> lock(Mtx);
        Busy = B;
        Notify ();
    }
    void PostIdle () {
        Post (0);
    }
    void PostBusy () {
        Post (1);
    }
};

// class to contain info needed to start a job on a waiting thread
// one of these is owned by each worker
class JobCtrl;
extern void BackGroundWorker (JobCtrl *);
class JobCtrl {
    private:

    public:
    int          Idx;  // sequential count to assist debug
    thread      *Thr;  // pointer to thread control structure 
    BusyLock     Busy; // tells worker when there's work to be done

    // tells the thread what type of work to do
    enum {
        CreateFile = 1, // create archive file
        ExtractFile   , // extract file from archive
    } JobType;

    union JI {
        struct {
            ArchFileCreate *AF;
            bool            Keep;
        } CreateFile;
        struct {
            ArchiveRead    *Arch;
            string          FileLine;
            uint64_t        LineNo;
        } ExtractFile;
         JI () {}
        ~JI () {}
    } JobInfo;

    JobCtrl (int idx) {
        Idx = idx;
        Thr = new thread (BackGroundWorker, this);
    }
    ~JobCtrl () {
        delete Thr;
    }
    void Join() {
        Thr->join();
    }
    void Notify () {
        Busy.Notify();
    }
    void Go () {
        Busy.PostBusy();
    }
};

// list of thread control structures
// each element of the list represents a thread
// which is available to start a new job
class ThreadPool_t {
    vector <JobCtrl *> All;   // list of all Worker threads
    vector <JobCtrl *> Avail; // set of Worker threads which are ready to perform work
    mutex              Mtx;   // mutex to control alloc/release
    condition_variable CV;    // condition variable for alloc/release

    public:

    ThreadPool_t () {
        DBGCTOR;
    }

    ~ThreadPool_t () {
        DBGDTOR;
        JoinAll();
        for (auto &Job : All)
            delete Job;
    }

    void     AddThreads    (int N);
    JobCtrl *AllocThread   (bool Wait = 1);
    void     ReleaseThread (JobCtrl *Job);
    void     WaitIdle      ();  // wait for all jobs to finish
    void     JoinAll       ();
};

// global job pool
extern ThreadPool_t ThreadPool;

#endif // THREADPOOL_H
