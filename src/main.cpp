#include "search/vanity_search.h"

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define PREFIX_LITERAL(value) value,
static const char *const default_prefixes[] = {VANITY_PREFIXES(PREFIX_LITERAL)};
#undef PREFIX_LITERAL

static bool parse_positive_int(const char *value, const char *option, int &result)
{
	if (value == nullptr || value[0] == '\0' || value[0] == '-')
	{
		std::fprintf(stderr, "%s requires a positive integer\n", option);
		return false;
	}

	errno = 0;
	char *end = nullptr;
	long parsed = std::strtol(value, &end, 10);
	if (errno != 0 || end == value || *end != '\0' || parsed <= 0 || parsed > INT_MAX)
	{
		std::fprintf(stderr, "%s requires a positive integer, got '%s'\n", option, value);
		return false;
	}
	result = static_cast<int>(parsed);
	return true;
}

static bool valid_prefix(const char *prefix)
{
	static const char *const base58_digits =
		"123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
	size_t length = std::strlen(prefix);
	if (length == 0 || length > 44)
	{
		return false;
	}
	for (size_t index = 0; index < length; ++index)
	{
		if (std::strchr(base58_digits, prefix[index]) == nullptr)
		{
			return false;
		}
	}
	return true;
}

static void print_usage(const char *program)
{
	std::printf("Usage: %s [--prefix PREFIX]... [--stop-after COUNT] [--max-iterations COUNT]\n",
				program);
	std::printf("Without --prefix, the prefixes from config.h are used.\n");
}

static bool parse_options(int argc, char **argv, vanity_options &options)
{
	options.stop_after_keys_found = STOP_AFTER_KEYS_FOUND;
	options.max_iterations = MAX_ITERATIONS;
	options.prefix_count = 0;

	bool custom_prefixes = false;
	for (int index = 1; index < argc; ++index)
	{
		const char *argument = argv[index];
		if (std::strcmp(argument, "--help") == 0 || std::strcmp(argument, "-h") == 0)
		{
			print_usage(argv[0]);
			return false;
		}
		if (std::strcmp(argument, "--prefix") == 0)
		{
			if (index + 1 >= argc || !valid_prefix(argv[index + 1]))
			{
				std::fprintf(stderr, "--prefix requires a valid non-empty Base58 prefix\n");
				return false;
			}
			if (!custom_prefixes)
			{
				options.prefix_count = 0;
				custom_prefixes = true;
			}
			if (options.prefix_count >= MAX_PATTERNS)
			{
				std::fprintf(stderr, "At most %d prefixes are supported\n", MAX_PATTERNS);
				return false;
			}
			options.prefixes[options.prefix_count++] = argv[++index];
			continue;
		}
		if (std::strcmp(argument, "--stop-after") == 0 ||
			std::strcmp(argument, "--max-iterations") == 0)
		{
			if (index + 1 >= argc)
			{
				std::fprintf(stderr, "%s requires a value\n", argument);
				return false;
			}
			int *target = std::strcmp(argument, "--stop-after") == 0
				? &options.stop_after_keys_found
				: &options.max_iterations;
			if (!parse_positive_int(argv[++index], argument, *target))
			{
				return false;
			}
			continue;
		}
		std::fprintf(stderr, "Unknown argument: %s\n", argument);
		print_usage(argv[0]);
		return false;
	}

	if (!custom_prefixes)
	{
		options.prefix_count = static_cast<int>(sizeof(default_prefixes) / sizeof(default_prefixes[0]));
		for (int index = 0; index < options.prefix_count; ++index)
		{
			options.prefixes[index] = default_prefixes[index];
		}
	}
	return true;
}

int main(int argc, char **argv)
{
	vanity_options options = {};
	if (!parse_options(argc, argv, options))
	{
		return argc > 1 && (std::strcmp(argv[1], "--help") == 0 ||
							std::strcmp(argv[1], "-h") == 0)
				? 0
				: 1;
	}
	return run_vanity_search(options);
}
