#pragma once
#include "journal_reader.h"
/* Internal borrowed read-only view, solely for existing read-only typed codecs.
 * No ownership transfer, append/apply/rollback/close allowed. */
L4Journal* l4_journal_reader_codec_view(L4JournalReader* reader);
