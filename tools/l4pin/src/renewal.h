#ifndef L4PIN_RENEWAL_H
#define L4PIN_RENEWAL_H
#include <windows.h>
#include <stdbool.h>
HANDLE enrollment_lock(void);
int renewal_run(const char* pin, const char* url);
#endif
