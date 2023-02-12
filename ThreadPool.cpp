#include "ThreadPool.h"
#include "Archive.h"
#include "Comp.h"

// global thread pool structure
ThreadPool_t ThreadPool;

// when set, all threads will quit
volatile int StopThreads = 0;

// how many threads are allocated for the current call tree
thread_local unsigned ThreadDepth = 0;

// thread top level function
void BackGroundWorker (JobCtrl *Job) {
    try {
        // process jobs until top level says we're done
        while (1) {
            // wait for a new job
            DBG ("Job #%d, Wait for busy\n", Job->Idx);
            Job->Busy.WaitBusy ();
            if (StopThreads) {
                DBG ("Job #%d Stopping\n", Job->Idx);
                Job->Busy.PostIdle();
                break;
            }

            // keep track of thread call depth
            ThreadDepth = Job->ParentDepth + 1;

            // process the job
            DBG ("Job #%d, Starting\n", Job->Idx);
            assert (Job->Task);
                 (*(Job->Task))();
            delete  Job->Task;
            DBG ("Job #%d, Finished\n", Job->Idx);

            // release the thread to the pool
            Job->Busy.PostIdle();
            ThreadPool.ReleaseThread (Job);
        }
    }

    catch (PB_Exception &E) {
        E.Handle();
    }
}

// add n threads to the pool
void ThreadPool_t::AddThreads (int N) {
    // get the mutex
    unique_lock<recursive_mutex> lock(Mtx);

    // create the threads
    for (int i = 0; i < N; i++) {
        int Idx = All.size()+1;
        JobCtrl *Job = new JobCtrl (Idx);
        All  .push_back(Job);
        Avail.push_back(Job);
        if (Idx < JobArraySize)
            JobArray [Idx] = Job;
    }

    // tell any waiting threads
    CV.notify_all();
}

void ThreadPool_t::WaitIdle () {
    unique_lock<recursive_mutex> lock(Mtx);
    CV.wait (lock, [this]{return All.size() == Avail.size();});
}

// call this after all work for the threads is complete
void ThreadPool_t::JoinAll () {
    DBG ("ThreadPool_t::JoinAll()\n");
    StopThreads = 1;

    // tell any waiting threads
    for (auto &Job : All) {
        Job->Notify();
        Job->Join();
    }
}

class DualLock {
    recursive_mutex *lock1;
    recursive_mutex *lock2;

    public:
    DualLock (recursive_mutex *l1, recursive_mutex *l2) {
        lock1 = l1;
        lock2 = l2;
        lock();
    }
    void lock () {
        std::lock (*lock1, *lock2);
    }
    void unlock () {
        lock1->unlock();
        lock2->unlock();
    }
};

// allocate a thread to be used for a job
JobCtrl * ThreadPool_t::AllocThread (bool Wait) {
    // get both mutex's
    DualLock dl (&Mtx, &BusyLocksMtx);

    AllocWaiting += Wait;
    CV.notify_all();  // tell others about AllocWaiting update

    // make sure we release the locks when we exit
    lock_guard <recursive_mutex> lock1 (Mtx         , std::adopt_lock);
    lock_guard <recursive_mutex> lock2 (BusyLocksMtx, std::adopt_lock);

    // wait for available thread
    // abort wait if caller doesn't want to wait
    // abort wait if all threads are waiting on busy lock
    CV.wait (dl, [this, Wait] {
                        return !Avail.empty()
                            || !Wait
                            || (BusyLocksWaiting + AllocWaiting + 1) >= All.size()
                            ;
                       });

    JobCtrl *Thr = NULL;
    if (!Avail.empty()) {
        // grab a thread
        Thr = Avail.back();
        Avail.pop_back();
    }

    AllocWaiting -= Wait;
    CV.notify_all();

    return Thr;
}

void ThreadPool_t::ReleaseThread (JobCtrl *Job) {
    // get the mutex
    unique_lock<recursive_mutex> lock(Mtx);

    // add the job to the queue
    Avail.push_back (Job);

    CV.notify_all();
}

void ThreadPool_t::Execute (function <void()> &Task, bool Wait) {
    auto T = new function <void()> (Task);
    Execute (T, Wait);
}

void ThreadPool_t::Execute (function <void()> *Task, bool Wait) {
    JobCtrl *Thr = NULL;

    if (All.size())
        // try to allocate a thread
        Thr = AllocThread(Wait);

    if (Thr) {
        Thr->Task        = Task;
        Thr->ParentDepth = ThreadDepth;
        Thr->Go();
    } else {
        // execute the task in current thread
        (*Task)();
        delete Task;
    }
}
