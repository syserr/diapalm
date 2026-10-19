/* The new/edit entry form. */

#ifndef DIAPALM_EDIT_H
#define DIAPALM_EDIT_H

#include <PalmOS.h>

/* Opens the form on an existing record, or on a blank entry when
   storageIndex is DP_NEW_ENTRY. */
void DpEditOpen(UInt16 storageIndex);

Boolean DpEditFormHandleEvent(EventPtr eventP);

#endif /* DIAPALM_EDIT_H */
