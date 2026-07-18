#ifndef SEARCH_BASE58_PREFIX_CUH
#define SEARCH_BASE58_PREFIX_CUH

#include <assert.h>
#include <stdint.h>
#include <string.h>

static int const BASE58_MAX_LENGTH = 44;
static int const PREFIX_LENGTH_VARIANTS = 2;

typedef struct
{
	uint8_t lower[PREFIX_LENGTH_VARIANTS][32];
	uint8_t upper[PREFIX_LENGTH_VARIANTS][32];
	uint8_t slow_path_limit[32];
	char prefix[BASE58_MAX_LENGTH + 1];
	uint8_t prefix_length;
	uint8_t leading_ones;
	uint8_t range_valid[PREFIX_LENGTH_VARIANTS];
	uint8_t upper_unbounded[PREFIX_LENGTH_VARIANTS];
} prefix_range;

extern __constant__ prefix_range device_prefix_ranges[MAX_PATTERNS];
extern __constant__ int device_prefix_count;

static int base58_digit(char character)
{
	static const char base58_digits[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
	const char *position = strchr(base58_digits, character);
	return position == NULL ? -1 : static_cast<int>(position - base58_digits);
}

static bool add_small(uint8_t *value, int first_byte, uint8_t amount)
{
	int carry = amount;
	for (int index = 31; index >= first_byte && carry != 0; --index)
	{
		int sum = value[index] + carry;
		value[index] = static_cast<uint8_t>(sum);
		carry = sum >> 8;
	}
	return carry == 0;
}

static bool multiply_small(uint8_t *value, int first_byte, uint8_t factor)
{
	int carry = 0;
	for (int index = 31; index >= first_byte; --index)
	{
		int product = value[index] * factor + carry;
		value[index] = static_cast<uint8_t>(product);
		carry = product >> 8;
	}
	return carry == 0;
}

static int max_base58_length(int byte_count)
{
	uint8_t power[32] = {0};
	power[31] = 1;
	int length = 1;
	while (multiply_small(power, 32 - byte_count, 58))
	{
		++length;
	}
	return length;
}

static bool build_base58_boundary(const char *suffix, int suffix_length, int encoded_length,
								  int byte_count, bool increment_prefix, uint8_t *boundary)
{
	memset(boundary, 0, 32);
	for (int index = 0; index < suffix_length; ++index)
	{
		int digit = base58_digit(suffix[index]);
		if (digit < 0 || !multiply_small(boundary, 32 - byte_count, 58) ||
			!add_small(boundary, 32 - byte_count, static_cast<uint8_t>(digit)))
		{
			return false;
		}
	}
	if (increment_prefix && !add_small(boundary, 32 - byte_count, 1))
	{
		return false;
	}
	for (int index = suffix_length; index < encoded_length; ++index)
	{
		if (!multiply_small(boundary, 32 - byte_count, 58))
		{
			return false;
		}
	}
	return true;
}

static void build_prefix_range(const char *prefix, prefix_range &range)
{
	memset(&range, 0, sizeof(range));
	range.prefix_length = static_cast<uint8_t>(strlen(prefix));
	assert(range.prefix_length > 0 && range.prefix_length <= BASE58_MAX_LENGTH);
	memcpy(range.prefix, prefix, range.prefix_length + 1);

	while (range.leading_ones < range.prefix_length && prefix[range.leading_ones] == '1')
	{
		++range.leading_ones;
	}

	int byte_count = 32 - range.leading_ones;
	int suffix_length = range.prefix_length - range.leading_ones;
	if (byte_count <= 0 || suffix_length == 0)
	{
		return;
	}

	int max_length = max_base58_length(byte_count);
	for (int variant = 0; variant < PREFIX_LENGTH_VARIANTS; ++variant)
	{
		int encoded_length = max_length - variant;
		if (encoded_length < suffix_length)
		{
			continue;
		}
		range.range_valid[variant] = build_base58_boundary(prefix + range.leading_ones,
														   suffix_length, encoded_length, byte_count, false, range.lower[variant]);
		range.upper_unbounded[variant] = !build_base58_boundary(prefix + range.leading_ones,
																suffix_length, encoded_length, byte_count, true, range.upper[variant]);
	}

	int fallback_length = max_length - PREFIX_LENGTH_VARIANTS + 1;
	if (fallback_length > 0)
	{
		build_base58_boundary("1", 1, fallback_length, byte_count, false, range.slow_path_limit);
	}
}

static __device__ int compare_u256(const uint8_t *left, const uint8_t *right)
{
	for (int index = 0; index < 32; ++index)
	{
		if (left[index] != right[index])
		{
			return left[index] < right[index] ? -1 : 1;
		}
	}
	return 0;
}

static __device__ bool matches_exact_prefix(const char *key, const prefix_range &range)
{
	for (int index = 0; index < range.prefix_length; ++index)
	{
		if (key[index] != range.prefix[index])
		{
			return false;
		}
	}
	return true;
}

static __device__ bool has_required_leading_zeros(const uint8_t *public_key,
												  const prefix_range &range)
{
	for (int index = 0; index < range.leading_ones; ++index)
	{
		if (public_key[index] != 0)
		{
			return false;
		}
	}
	return true;
}

static __device__ int matching_prefix_range(const uint8_t *public_key)
{
	for (int pattern = 0; pattern < device_prefix_count; ++pattern)
	{
		const prefix_range &range = device_prefix_ranges[pattern];
		if (!has_required_leading_zeros(public_key, range))
		{
			continue;
		}
		if (range.prefix_length == range.leading_ones)
		{
			return pattern;
		}
		for (int variant = 0; variant < PREFIX_LENGTH_VARIANTS; ++variant)
		{
			if (!range.range_valid[variant] || compare_u256(public_key, range.lower[variant]) < 0)
			{
				continue;
			}
			if (range.upper_unbounded[variant] || compare_u256(public_key, range.upper[variant]) < 0)
			{
				return pattern;
			}
		}
	}
	return -1;
}

static __device__ bool needs_base58_fallback(const uint8_t *public_key)
{
	for (int pattern = 0; pattern < device_prefix_count; ++pattern)
	{
		const prefix_range &range = device_prefix_ranges[pattern];
		if (has_required_leading_zeros(public_key, range) &&
			range.prefix_length > range.leading_ones &&
			compare_u256(public_key, range.slow_path_limit) < 0)
		{
			return true;
		}
	}
	return false;
}

static __device__ bool base58_encode_device(char *output, size_t *output_size,
											uint8_t *data, size_t input_size)
{
	const char digits[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
	const uint8_t *input = data;
	int carry;
	size_t index;
	size_t digit_index;
	size_t high;
	size_t zero_count = 0;

	while (zero_count < input_size && !input[zero_count])
	{
		++zero_count;
	}

	size_t size = (input_size - zero_count) * 138 / 100 + 1;
	uint8_t buffer[45];
	memset(buffer, 0, size);
	for (index = zero_count, high = size - 1; index < input_size; ++index, high = digit_index)
	{
		for (carry = input[index], digit_index = size - 1;
			 (digit_index > high) || carry; --digit_index)
		{
			carry += 256 * buffer[digit_index];
			buffer[digit_index] = carry % 58;
			carry /= 58;
			if (!digit_index)
			{
				break;
			}
		}
	}

	for (digit_index = 0; digit_index < size && !buffer[digit_index]; ++digit_index)
	{
	}
	if (*output_size <= zero_count + size - digit_index)
	{
		*output_size = zero_count + size - digit_index + 1;
		return false;
	}

	if (zero_count)
	{
		memset(output, '1', zero_count);
	}
	for (index = zero_count; digit_index < size; ++index, ++digit_index)
	{
		output[index] = digits[buffer[digit_index]];
	}
	output[index] = '\0';
	*output_size = index + 1;
	return true;
}

#endif