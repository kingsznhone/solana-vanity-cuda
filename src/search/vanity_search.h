#ifndef VANITY_ENGINE_H
#define VANITY_ENGINE_H

#include "../config.h"

struct vanity_options
{
	int stop_after_keys_found;
	int max_iterations;
	int prefix_count;
	const char *prefixes[MAX_PATTERNS];
};

int run_vanity_search(const vanity_options &options);

#endif
