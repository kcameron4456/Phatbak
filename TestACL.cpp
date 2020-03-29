#include "Types.h"
#include "Logging.h"
#include "Utils.h"
#include "Comp.h"
using namespace Utils;

#include <sys/types.h>  /* Type definitions used by many programs */
#include <stdio.h>      /* Standard I/O functions */
#include <stdlib.h>     /* Prototypes of commonly used library functions,
                           plus EXIT_SUCCESS and EXIT_FAILURE constants */
#include <unistd.h>     /* Prototypes for many system calls */
#include <errno.h>      /* Declares errno and defines error constants */
#include <string.h>     /* Commonly used string-handling functions */

typedef enum { FALSE, TRUE } Boolean;

#define min(m,n) ((m) < (n) ? (m) : (n))
#define max(m,n) ((m) > (n) ? (m) : (n))

char *userNameFromId(uid_t uid);

uid_t userIdFromName(const char *name);

char *groupNameFromId(gid_t gid);

gid_t groupIdFromName(const char *name);

#include <acl/libacl.h>
#include <sys/acl.h>
#include <stdio.h>
using namespace std;

int main (int argc, char **argv) {
    O.CompType = CompType_ZTSD;
    try {
        for (int i = 1; i < argc; i++) {
            char *arg = argv[i];
printf ("arg=%s\n", arg);

            for (int j = 0; j < 2; j++) {
                const char *tname = j ? "default"        : "regular";
                acl_type_t  ttype = j ? ACL_TYPE_DEFAULT : ACL_TYPE_ACCESS;

                auto acl = acl_get_file(arg, ttype);
                if (!acl && j)
                    continue;
                if (!acl)
                    THROW_PBEXCEPTION_IO ("Can't get acl for: %s", arg);

                char *acltext = acl_to_any_text (acl, NULL, ',', TEXT_ABBREVIATE);
                if (!acltext)
                    THROW_PBEXCEPTION_IO ("Can't get acl text for: %s", arg);
                printf ("acltext (%s) = %s\n", tname, acltext);

                // remove base acl entries
                vecstr Entries = SplitStr (acltext, ",");
                vecstr Filtered;
                for (auto &Entry : Entries) {
                    if (Entry.find_first_of ("ugo") != 0) {
                        Filtered.push_back(Entry);
                        continue;
                    }
                    if (Entry.find ("::") != 1) {
                        Filtered.push_back(Entry);
                        continue;
                    }
                }
                string FilteredStr = JoinStrs (Filtered, ",");
                printf ("acltext filtered (%s) = %s\n", tname, FilteredStr.c_str());

                int EntCnt = acl_entries (acl);
                printf ("(%s) # acl entries= %d\n", tname, EntCnt);

                acl_entry_t Entry;
                for (int EntStat = acl_get_entry (acl, ACL_FIRST_ENTRY, &Entry);
                         EntStat == 1;
                         EntStat = acl_get_entry (acl, ACL_NEXT_ENTRY , &Entry)) {
                    acl_tag_t tag;
                    acl_get_tag_type(Entry, &tag);
                    printf("%-12s", (tag == ACL_USER_OBJ) ?  "user_obj" :
                                    (tag == ACL_USER) ?      "user" :
                                    (tag == ACL_GROUP_OBJ) ? "group_obj" :
                                    (tag == ACL_GROUP) ?     "group" :
                                    (tag == ACL_MASK) ?      "mask" :
                                    (tag == ACL_OTHER) ?     "other" :
                                    "???");

                    uid_t *uidp;
                    gid_t *gidp;
                    acl_permset_t permset;
                    int permVal;
                    char * name;
                    
                    if (tag == ACL_USER) {
                        uidp = (uid_t*)acl_get_qualifier(Entry);
                        if (!uidp)
                            THROW_PBEXCEPTION_IO ("acl_get_qualifier");

                            printf("%-8d ", *uidp);

                        if (acl_free(uidp) == -1)
                            THROW_PBEXCEPTION_IO ("acl_free");

                    } else if (tag == ACL_GROUP) {
                        gidp = (gid_t *)acl_get_qualifier(Entry);
                        if (gidp == NULL)
                            THROW_PBEXCEPTION_IO ("acl_get_qualifier");

                            printf("%-8d ", *gidp);

                        if (acl_free(gidp) == -1)
                            THROW_PBEXCEPTION_IO ("acl_free");

                    } else {
                        printf("         ");
                    }

                    /* Retrieve and display permissions */

                    if (acl_get_permset(Entry, &permset) == -1)
                        THROW_PBEXCEPTION_IO ("acl_get_permset");

                    permVal = acl_get_perm(permset, ACL_READ);
                    if (permVal == -1)
                        THROW_PBEXCEPTION_IO ("acl_get_perm - ACL_READ");
                    printf("%c", (permVal == 1) ? 'r' : '-');
                    permVal = acl_get_perm(permset, ACL_WRITE);
                    if (permVal == -1)
                        THROW_PBEXCEPTION_IO ("acl_get_perm - ACL_WRITE");
                    printf("%c", (permVal == 1) ? 'w' : '-');
                    permVal = acl_get_perm(permset, ACL_EXECUTE);
                    if (permVal == -1)
                        THROW_PBEXCEPTION_IO ("acl_get_perm - ACL_EXECUTE");
                    printf("%c", (permVal == 1) ? 'x' : '-');

                    printf ("\n");
                }

                // step through all the acl permissions
                acl_t NewACL = acl_init(0);
                for (int EntStat = acl_get_entry (acl, ACL_FIRST_ENTRY, &Entry);
                         EntStat == 1;
                         EntStat = acl_get_entry (acl, ACL_NEXT_ENTRY , &Entry)) {

                    // get entry type
                    acl_tag_t tag;
                    acl_get_tag_type(Entry, &tag);

                    // ignore base permissions
                    if ( tag == ACL_USER_OBJ
                       ||tag == ACL_GROUP_OBJ
                       ||tag == ACL_OTHER
                       )
                        continue;

                    acl_entry_t NewEntry;
                    if (acl_create_entry (&NewACL, &NewEntry) < 0)
                        THROW_PBEXCEPTION_IO ("Can't create acl entry\n");
                    if (acl_copy_entry (NewEntry, Entry) < 0)
                        THROW_PBEXCEPTION_IO ("Can't copy acl entry\n");
                }

                char *NewTxt = acl_to_any_text (NewACL, NULL, ',', TEXT_ABBREVIATE);
                printf ("NewACL (%s) = %s\n", tname, NewTxt);

printf ("NewACL size = %ld\n", acl_size(NewACL));
                char ExtBuf [acl_size(NewACL)];
                acl_copy_ext (ExtBuf, NewACL, acl_size(NewACL));

                acl_t CopyAcl = acl_copy_int (ExtBuf);
                char *CopyTxt = acl_to_any_text (CopyAcl, NULL, ',', TEXT_ABBREVIATE);
                printf ("CopyAcl (%s) entries (%d) = %s\n", tname, acl_entries (CopyAcl), CopyTxt);

                string ExtStr (ExtBuf, acl_size(NewACL));
                string CompStr;
                Comp::Compress (CompType_ZTSD, ExtStr, CompStr);
                printf ("CopyAcl (%s) Ext size=%ld, Comp size=%ld\n", tname, ExtStr.size(), CompStr.size());

                acl_free (acltext);
                acl_free (acl);
            }

            string UtilsAcl = Utils::GetFileAcls (arg);
            printf ("UtilsAcl = %s\n", UtilsAcl.c_str());
        }
    }

    // handle exceptions
    catch (const char *msg) {
        fprintf (stderr, "Exception: %s\n", msg);
        return 1;
    }
    catch (PB_Exception &PBE) {
        PBE.Handle();
    }
}
