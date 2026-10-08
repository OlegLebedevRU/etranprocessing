/* Keep helper's existing immutable anchor reader, without its producer symbols.
 * Macro is local to this translation unit: native journal replay stays enabled. */
#define L4_RECOVERY_READER_ONLY
#include "../../l4common/recovery_store.c"
