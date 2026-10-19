/* CSV export: writes a "Diapalm Export" PDB with one record per line. */

#ifndef DIAPALM_EXPORT_H
#define DIAPALM_EXPORT_H

#include <PalmOS.h>

#define DP_EXPORT_NAME "Diapalm Export"
#define DP_EXPORT_TYPE 'DIEX'
#define DP_EXPORT_CREATOR 'dprd'

/* Rebuilds the export database from every log entry, oldest first. The first
   record is the header row. Returns the number of data rows written, or
   dmMaxRecordIndex on failure. */
UInt16 DpExportCsv(void);

#endif /* DIAPALM_EXPORT_H */
