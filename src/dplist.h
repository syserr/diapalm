/* The entry list form: newest entries first, one row per entry. */

#ifndef DIAPALM_LIST_H
#define DIAPALM_LIST_H

#include <PalmOS.h>

Boolean DpListFormHandleEvent(EventPtr eventP);

/* Storage index of the selected entry, or DP_NEW_ENTRY when nothing is
   selected. */
UInt16 DpListSelectedIndex(void);

#endif /* DIAPALM_LIST_H */
