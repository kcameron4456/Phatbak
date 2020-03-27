#ifndef BUSYLOCK_H
#define BUSYLOCK_H

#include <mutex>
#include <condition_variable>
using namespace std;

extern volatile int StopThreads;

// these are used to detect when all threads are waiting
// we can use this to break deadlocks
extern unsigned           BusyLocksWaiting; // number of BusyLocks currently in wait state
extern recursive_mutex    BusyLocksMtx;     // mutex to provide atomic update and test of above
extern condition_variable BusyLocksCV;      // used to inform threadpool when BusyLocksWaiting changes

#define BUSYLOCK_WINC {      \
    BusyLocksMtx.lock();     \
    BusyLocksWaiting ++;     \
    BusyLocksCV.notify_all();\
    BusyLocksMtx.unlock();   \
}

#define BUSYLOCK_WDEC {      \
    BusyLocksMtx.lock();     \
    BusyLocksWaiting --;     \
    BusyLocksCV.notify_all();\
    BusyLocksMtx.unlock();   \
}

class BusyLock {
    bool               Busy;
    mutex              Mtx;
    condition_variable CV;

    public:
    BusyLock (bool b = false) {
        Busy    = b;
    }
    ~BusyLock () {
    }
    void WaitIdle () {
        unique_lock<mutex> lock(Mtx);
        BUSYLOCK_WINC
        CV.wait (lock, [this]{return !Busy || StopThreads;});
        BUSYLOCK_WDEC
    }
    void WaitIdleAndPost () {
        unique_lock<mutex> lock(Mtx);
        BUSYLOCK_WINC
        CV.wait (lock, [this]{return !Busy || StopThreads;});
        Busy = 1;
        BUSYLOCK_WDEC
    }
    void WaitBusy () {
        unique_lock<mutex> lock(Mtx);
        BUSYLOCK_WINC
        CV.wait (lock, [this]{return Busy || StopThreads;});
        BUSYLOCK_WDEC
    }
    void WaitBusyAndPost () {
        unique_lock<mutex> lock(Mtx);
        BUSYLOCK_WINC
        CV.wait (lock, [this]{return Busy || StopThreads;});
        Busy = 0;
        BUSYLOCK_WDEC
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
    bool CheckIdle () {
        unique_lock<mutex> lock(Mtx);
        return !Busy;
    }
    bool CheckBusy () {
        unique_lock<mutex> lock(Mtx);
        return Busy;
    }
};

#endif // BUSYLOCK_H
