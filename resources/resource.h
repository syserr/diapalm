/* IDs shared between app.rcp and the C sources. */

#ifndef DIAPALM_RESOURCE_H
#define DIAPALM_RESOURCE_H

/* Menu bar (one MENU resource holding every pulldown) */
#define MenuBarId                1000

#define NewEntryItemId           1100
#define ExportItemId             1101
#define EditEntryItemId          1102
#define DeleteEntryItemId        1103
#define SetReminderItemId        1104
#define CancelReminderItemId     1105
#define AboutItemId              1106
#define A1cItemId                1107

/* Forms */
#define ListFormId               2000
#define EditFormId               2010
#define A1cFormId                2020

/* ListForm objects */
#define ListFormListId           2100

/* EditForm objects */
#define EditFormNoteId           2200
#define EditFormTimeLabelId      2201
#define EditFormBgFieldId        2202
#define EditFormInsulinFieldId   2203
#define EditFormCarbsFieldId     2204
#define EditFormSetTimeButtonId  2205
#define EditFormCancelButtonId   2206
#define EditFormDeleteButtonId   2207
#define EditFormOkButtonId       2208
#define EditFormBgLabelId        2209
#define EditFormInsulinLabelId   2210
#define EditFormCarbsLabelId     2211
#define EditFormNoteLabelId      2212

/* A1cForm objects */
#define A1cFormListId            2300
#define A1cFormValueFieldId      2301
#define A1cFormDateButtonId      2302
#define A1cFormAddButtonId       2303
#define A1cFormDeleteButtonId    2304
#define A1cFormDoneButtonId      2305
#define A1cFormValueLabelId      2306

/* Alerts */
#define DeleteAlertId            3000
#define ErrorAlertId             3001
#define NothingToSaveAlertId     3002
#define AboutAlertId             3003
#define ExportedAlertId          3004
#define ReminderSetAlertId       3005
#define ReminderClearedAlertId   3006
#define NoSelectionAlertId       3007
#define ExportFailedAlertId      3008
#define ReminderFailedAlertId    3009
#define DeleteA1cAlertId         3010
#define NoA1cSelectionAlertId    3011

/* Strings */
#define AppVersionId             1
#define AppNameStrId             4000
#define EmptyListStrId           4001
#define EmptyListHintStrId       4002

#endif /* DIAPALM_RESOURCE_H */
