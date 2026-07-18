#ifndef SEARCH_RESULT_WRITER_H
#define SEARCH_RESULT_WRITER_H

#include "match_record.h"

bool initialize_result_writer();
bool write_match(const match_record &match);

#endif