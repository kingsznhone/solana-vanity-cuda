#include "result_writer.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{

	std::ofstream result_output;
	std::string result_path;

	std::string base58_encode(const unsigned char *input, size_t input_size)
	{
		static const char alphabet[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
		size_t leading_zeroes = 0;
		while (leading_zeroes < input_size && input[leading_zeroes] == 0)
		{
			++leading_zeroes;
		}
		std::vector<unsigned char> digits(input_size * 138 / 100 + 1, 0);
		size_t length = 0;
		for (size_t input_index = leading_zeroes; input_index < input_size; ++input_index)
		{
			int carry = input[input_index];
			size_t digit_count = 0;
			for (std::vector<unsigned char>::reverse_iterator digit = digits.rbegin();
				 (digit_count < length || carry != 0) && digit != digits.rend(); ++digit, ++digit_count)
			{
				carry += 256 * *digit;
				*digit = static_cast<unsigned char>(carry % 58);
				carry /= 58;
			}
			length = digit_count;
		}
		std::string encoded(leading_zeroes, '1');
		for (std::vector<unsigned char>::const_iterator digit = digits.end() - length;
			 digit != digits.end(); ++digit)
		{
			encoded += alphabet[*digit];
		}
		return encoded;
	}

	bool ensure_result_directory()
	{
		const char *result_directory = getenv("VANITY_RESULT_DIR");
		if (result_directory == NULL || result_directory[0] == '\0')
		{
			result_directory = "result";
		}
		if (mkdir(result_directory, 0700) == 0 || errno == EEXIST)
		{
			struct stat status = {};
			if (stat(result_directory, &status) == 0 && S_ISDIR(status.st_mode))
			{
				return true;
			}
		}
		fprintf(stderr, "RESULT_WRITE_FAIL,cannot create result directory: %s\n", strerror(errno));
		return false;
	}

	std::string utc_timestamp(bool filename_safe)
	{
		const std::chrono::system_clock::time_point now =
			std::chrono::system_clock::now();
		const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
		struct tm utc_time = {};
		gmtime_r(&now_time, &utc_time);
		const long long milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
										   now.time_since_epoch())
										   .count() %
									   1000;
		std::ostringstream timestamp;
		timestamp << std::put_time(&utc_time, "%Y-%m-%dT%H")
				  << (filename_safe ? '-' : ':')
				  << std::put_time(&utc_time, "%M:%S")
				  << '.' << std::setfill('0') << std::setw(3) << milliseconds << 'Z';
		if (filename_safe)
		{
			std::string value = timestamp.str();
			for (char &character : value)
			{
				if (character == ':')
				{
					character = '-';
				}
			}
			return value;
		}
		return timestamp.str();
	}

} // namespace

bool initialize_result_writer()
{
	if (!ensure_result_directory())
	{
		return false;
	}
	const char *result_directory = getenv("VANITY_RESULT_DIR");
	if (result_directory == NULL || result_directory[0] == '\0')
	{
		result_directory = "result";
	}
	result_path = std::string(result_directory) + "/" +
				  utc_timestamp(true) + ".csv";
	result_output.open(result_path, std::ios::out | std::ios::trunc);
	if (!result_output)
	{
		fprintf(stderr, "RESULT_WRITE_FAIL,cannot create %s\n", result_path.c_str());
		return false;
	}
	if (chmod(result_path.c_str(), 0600) != 0)
	{
		fprintf(stderr, "RESULT_WRITE_FAIL,cannot protect %s: %s\n",
				result_path.c_str(), strerror(errno));
		result_output.close();
		return false;
	}
	result_output << "time,pubkey,privkey\n";
	result_output.flush();
	if (!result_output)
	{
		fprintf(stderr, "RESULT_WRITE_FAIL,cannot write CSV header to %s\n",
				result_path.c_str());
		result_output.close();
		return false;
	}
	printf("RESULT_FILE,path=%s\n", result_path.c_str());
	return true;
}

bool write_match(const match_record &match)
{
	unsigned char private_key[64];
	memcpy(private_key, match.seed, sizeof(match.seed));
	memcpy(private_key + sizeof(match.seed), match.public_key, sizeof(match.public_key));
	std::string public_key = base58_encode(match.public_key, sizeof(match.public_key));
	std::string encoded_private_key = base58_encode(private_key, sizeof(private_key));
	printf("GPU %d MATCH %s ,  privkey: %s\n", match.gpu, public_key.c_str(),
		   encoded_private_key.c_str());
	if (!result_output.is_open())
	{
		fprintf(stderr, "RESULT_WRITE_FAIL,result writer is not initialized\n");
		return false;
	}
	result_output << utc_timestamp(false) << ',' << public_key << ','
				  << encoded_private_key << '\n';
	result_output.flush();
	if (!result_output)
	{
		fprintf(stderr, "RESULT_WRITE_FAIL,cannot append to %s\n", result_path.c_str());
		return false;
	}
	return true;
}