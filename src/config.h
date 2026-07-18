#ifndef VANITY_CONFIG
#define VANITY_CONFIG

#ifndef VANITY_ATTEMPTS_PER_EXECUTION
#define VANITY_ATTEMPTS_PER_EXECUTION 10000
#endif

#ifndef VANITY_THREADS_PER_BLOCK
#define VANITY_THREADS_PER_BLOCK 512
#endif

#ifndef VANITY_REGISTER_LIMIT
#define VANITY_REGISTER_LIMIT "128"
#endif

#ifndef VANITY_USE_LAUNCH_BOUNDS
#define VANITY_USE_LAUNCH_BOUNDS 0
#endif

static int const MAX_ITERATIONS = 100000;
static int const STOP_AFTER_KEYS_FOUND = 1;

static int const MAX_PATTERNS = 16;

// Exact Base58 address prefixes. Define each prefix once for host-side range
// construction and device-side matching.
#define VANITY_PREFIXES(X) \
    X("AAAA")

#endif
