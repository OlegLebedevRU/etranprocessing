#ifndef L4CON_FM_USER_H
#define L4CON_FM_USER_H
#include "../../l4common/physical_console_token.h"
/* FM keeps the approved physical-console selection and actual OS permissions. */
static HANDLE fm_desktop_token(DWORD* session_id,LUID* authentication){
 return l4_physical_console_token(session_id,authentication);
}
#endif
