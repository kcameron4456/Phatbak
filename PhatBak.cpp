#include "Create.h"
#include "Extract.h"
#include "LiveFile.h"
#include "Logging.h"
#include "Opts.h"

#include <stdio.h>

int main (int argc, const char **argv) {
    try {
        O.ParseCmdLine (argc, argv);

        if (O.Operation == Opts::DoCreate) {
            Create C;
            for (auto Dir : O.FileArgs)
                C.DoCreate (CanonizeFileName (Dir));
        } else if (O.Operation == Opts::DoExtract) {
            Extract E (O);
            E.DoExtract (); // TBD: use file args to extract subset of archive
        }
    }

    // handle exceptions
    catch (const char *msg) {
        fprintf (stderr, "%s\n", msg);
        return 1;
    }
    catch (PB_Exception &PBE) {
        PBE.Handle();
    }

    return 0;
}
