#ifndef THREADPOOL_H
#define THREADPOOL_H

#include "Logging.h"
#include "Archive.h"
#include "BusyLock.h"

#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
using namespace std;

// how many threads are allocated for the current call tree
extern thread_local unsigned ThreadDepth;

// class to contain info needed to start a job on a waiting thread
// one of these is owned by each worker
class JobCtrl;
extern void BackGroundWorker (JobCtrl *);
class JobCtrl {
    private:

    public:
    int               Idx;         // sequential count to assist debug
    thread           *Thr;         // pointer to thread control structure 
    BusyLock          Busy;        // tells worker when there's work to be done
    unsigned          ParentDepth; // thread call depth of requester

    // generic self-contained task
    function <void()> *Task;

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
    vector <JobCtrl *>     All;   // list of all Worker threads
    vector <JobCtrl *>     Avail; // set of Worker threads which are ready to perform work
    recursive_mutex        Mtx;   // mutex to protect access to the all and avail queues
    condition_variable_any CV;    // condition variable for alloc/release

    // for helping debug with gdb
    static const int JobArraySize = 500;
    JobCtrl *JobArray [JobArraySize];

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
    void     WaitIdle      ();  // wait for all jobs to finish
    void     JoinAll       ();
    JobCtrl *AllocThread   (bool Wait = 1);
    void     ReleaseThread (JobCtrl *Job);
    void     Execute       (function <void()> &Task, bool Wait = 1);
    void     Execute       (function <void()> *Task, bool Wait = 1);
};

// global job pool
extern ThreadPool_t ThreadPool;

#endif // THREADPOOL_H
