# include "BusyLock.h"

unsigned           BusyLocksWaiting; // number of BusyLocks currently in wait state
recursive_mutex    BusyLocksMtx;     // mutex to provide atomic update and test of above
condition_variable BusyLocksCV;      // used to inform threadpool when BusyLocksWaiting changes
