#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <array>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "fixed16_table.h"
#include "fixed16_sha256.h"
#include "fixed16_table_tool.h"

extern "C"
{
	typedef int32_t fe[10];
	void fe_0(fe h);
	void fe_1(fe h);
	void fe_frombytes(fe h, const unsigned char *s);
	void fe_tobytes(unsigned char *s, const fe h);
	void fe_copy(fe h, const fe f);
	int fe_isnegative(const fe f);
	int fe_isnonzero(const fe f);
	void fe_neg(fe h, const fe f);
	void fe_add(fe h, const fe f, const fe g);
	void fe_invert(fe out, const fe z);
	void fe_sq(fe h, const fe f);
	void fe_sq2(fe h, const fe f);
	void fe_mul(fe h, const fe f, const fe g);
	void fe_pow22523(fe out, const fe z);
	void fe_sub(fe h, const fe f, const fe g);
}

namespace
{

	struct point_p3
	{
		fe X;
		fe Y;
		fe Z;
		fe T;
	};

	struct point_p1p1
	{
		fe X;
		fe Y;
		fe Z;
		fe T;
	};

	struct point_cached
	{
		fe YplusX;
		fe YminusX;
		fe Z;
		fe T2d;
	};

	struct precomp
	{
		fe yplusx;
		fe yminusx;
		fe xy2d;
	};

	static_assert(sizeof(int32_t) == 4, "fixed16 format requires 32-bit int32_t");
	static_assert(sizeof(fe) == 40, "fixed16 format requires ref10 fe[10]");
	static_assert(sizeof(precomp) == FIXED16_TABLE_ENTRY_SIZE,
				  "fixed16 format requires a 120-byte precomp entry");

	const fe curve_d = {
		-10913610, 13857413, -15372611, 6949391, 114729,
		-8787816, -6275908, -3247719, -18696448, -12055116};

	const fe curve_d2 = {
		-21827239, -5839606, -30745221, 13898782, 229458,
		15978800, -12551817, -6495438, 29715968, 9444199};

	const fe sqrt_minus_one = {
		-32595792, -7943725, 9377950, 3500415, 12389472,
		-272473, -25146209, -2005654, 326686, 11406482};

	const unsigned char compressed_basepoint[32] = {
		0x58, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
		0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
		0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
		0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66};

	void point_to_cached(point_cached *result, const point_p3 *point)
	{
		fe_add(result->YplusX, point->Y, point->X);
		fe_sub(result->YminusX, point->Y, point->X);
		fe_copy(result->Z, point->Z);
		fe_mul(result->T2d, point->T, curve_d2);
	}

	void point_add(point_p1p1 *result, const point_p3 *point, const point_cached *addend)
	{
		fe temporary;
		fe_add(result->X, point->Y, point->X);
		fe_sub(result->Y, point->Y, point->X);
		fe_mul(result->Z, result->X, addend->YplusX);
		fe_mul(result->Y, result->Y, addend->YminusX);
		fe_mul(result->T, addend->T2d, point->T);
		fe_mul(result->X, point->Z, addend->Z);
		fe_add(temporary, result->X, result->X);
		fe_sub(result->X, result->Z, result->Y);
		fe_add(result->Y, result->Z, result->Y);
		fe_add(result->Z, temporary, result->T);
		fe_sub(result->T, temporary, result->T);
	}

	void point_p1p1_to_p3(point_p3 *result, const point_p1p1 *point)
	{
		fe_mul(result->X, point->X, point->T);
		fe_mul(result->Y, point->Y, point->Z);
		fe_mul(result->Z, point->Z, point->T);
		fe_mul(result->T, point->X, point->Y);
	}

	void point_double(point_p3 *result, const point_p3 *point)
	{
		point_p1p1 completed;
		fe temporary;
		fe_sq(completed.X, point->X);
		fe_sq(completed.Z, point->Y);
		fe_sq2(completed.T, point->Z);
		fe_add(completed.Y, point->X, point->Y);
		fe_sq(temporary, completed.Y);
		fe_add(completed.Y, completed.Z, completed.X);
		fe_sub(completed.Z, completed.Z, completed.X);
		fe_sub(completed.X, temporary, completed.Y);
		fe_sub(completed.T, completed.T, completed.Z);
		point_p1p1_to_p3(result, &completed);
	}

	bool decode_basepoint(point_p3 *point)
	{
		fe u;
		fe v;
		fe v3;
		fe vxx;
		fe check;
		fe_frombytes(point->Y, compressed_basepoint);
		fe_1(point->Z);
		fe_sq(u, point->Y);
		fe_mul(v, u, curve_d);
		fe_sub(u, u, point->Z);
		fe_add(v, v, point->Z);
		fe_sq(v3, v);
		fe_mul(v3, v3, v);
		fe_sq(point->X, v3);
		fe_mul(point->X, point->X, v);
		fe_mul(point->X, point->X, u);
		fe_pow22523(point->X, point->X);
		fe_mul(point->X, point->X, v3);
		fe_mul(point->X, point->X, u);
		fe_sq(vxx, point->X);
		fe_mul(vxx, vxx, v);
		fe_sub(check, vxx, u);
		if (fe_isnonzero(check))
		{
			fe_add(check, vxx, u);
			if (fe_isnonzero(check))
			{
				return false;
			}
			fe_mul(point->X, point->X, sqrt_minus_one);
		}
		// ge_frombytes_negate_vartime returns the opposite X sign. The standard
		// compressed base point has sign bit zero, so choose non-negative X.
		if (fe_isnegative(point->X))
		{
			fe_neg(point->X, point->X);
		}
		fe_mul(point->T, point->X, point->Y);
		return true;
	}

	void point_to_bytes(unsigned char output[32], const point_p3 *point)
	{
		fe inverse_z;
		fe x;
		fe y;
		fe_invert(inverse_z, point->Z);
		fe_mul(x, point->X, inverse_z);
		fe_mul(y, point->Y, inverse_z);
		fe_tobytes(output, y);
		output[31] ^= static_cast<unsigned char>(fe_isnegative(x) << 7);
	}

	void canonicalize(fe value)
	{
		unsigned char encoded[32];
		fe_tobytes(encoded, value);
		fe_frombytes(value, encoded);
	}

	bool field_is_zero(const fe value)
	{
		return fe_isnonzero(value) == 0;
	}

	void store_u32_le(unsigned char *output, uint32_t value)
	{
		output[0] = static_cast<unsigned char>(value);
		output[1] = static_cast<unsigned char>(value >> 8);
		output[2] = static_cast<unsigned char>(value >> 16);
		output[3] = static_cast<unsigned char>(value >> 24);
	}

	void store_u64_le(unsigned char *output, uint64_t value)
	{
		for (int byte = 0; byte < 8; ++byte)
		{
			output[byte] = static_cast<unsigned char>(value >> (8 * byte));
		}
	}

	uint32_t load_u32_le(const unsigned char *input)
	{
		return static_cast<uint32_t>(input[0]) |
			   (static_cast<uint32_t>(input[1]) << 8) |
			   (static_cast<uint32_t>(input[2]) << 16) |
			   (static_cast<uint32_t>(input[3]) << 24);
	}

	uint64_t load_u64_le(const unsigned char *input)
	{
		uint64_t value = 0;
		for (int byte = 0; byte < 8; ++byte)
		{
			value |= static_cast<uint64_t>(input[byte]) << (8 * byte);
		}
		return value;
	}

	void serialize_entry(unsigned char output[FIXED16_TABLE_ENTRY_SIZE], const precomp &entry)
	{
		const int32_t *limbs = &entry.yplusx[0];
		for (uint32_t limb = 0; limb < FIXED16_TABLE_LIMBS_PER_ENTRY; ++limb)
		{
			store_u32_le(output + limb * 4, static_cast<uint32_t>(limbs[limb]));
		}
	}

	void make_header(unsigned char header[FIXED16_TABLE_HEADER_SIZE], const unsigned char digest[32])
	{
		memset(header, 0, FIXED16_TABLE_HEADER_SIZE);
		memcpy(header, FIXED16_TABLE_MAGIC, 8);
		store_u32_le(header + 8, FIXED16_TABLE_HEADER_SIZE);
		store_u32_le(header + 12, FIXED16_TABLE_FORMAT_VERSION);
		store_u32_le(header + 16, FIXED16_TABLE_GENERATOR_VERSION);
		store_u32_le(header + 20, FIXED16_TABLE_WINDOW_BITS);
		store_u32_le(header + 24, FIXED16_TABLE_POSITIONS);
		store_u32_le(header + 28, FIXED16_TABLE_VALUES);
		store_u32_le(header + 32, FIXED16_TABLE_ENTRY_SIZE);
		store_u64_le(header + 36, FIXED16_TABLE_ENTRY_COUNT);
		store_u64_le(header + 44, FIXED16_TABLE_PAYLOAD_SIZE);
		memcpy(header + 52, digest, 32);
	}

	bool validate_header(const unsigned char header[FIXED16_TABLE_HEADER_SIZE], std::string *error)
	{
		if (memcmp(header, FIXED16_TABLE_MAGIC, 8) != 0)
		{
			*error = "bad magic";
		}
		else if (load_u32_le(header + 8) != FIXED16_TABLE_HEADER_SIZE)
		{
			*error = "unsupported header size";
		}
		else if (load_u32_le(header + 12) != FIXED16_TABLE_FORMAT_VERSION)
		{
			*error = "unsupported format version";
		}
		else if (load_u32_le(header + 16) != FIXED16_TABLE_GENERATOR_VERSION)
		{
			*error = "unsupported generator version";
		}
		else if (load_u32_le(header + 20) != FIXED16_TABLE_WINDOW_BITS ||
				 load_u32_le(header + 24) != FIXED16_TABLE_POSITIONS ||
				 load_u32_le(header + 28) != FIXED16_TABLE_VALUES)
		{
			*error = "unexpected table dimensions";
		}
		else if (load_u32_le(header + 32) != FIXED16_TABLE_ENTRY_SIZE ||
				 load_u64_le(header + 36) != FIXED16_TABLE_ENTRY_COUNT ||
				 load_u64_le(header + 44) != FIXED16_TABLE_PAYLOAD_SIZE)
		{
			*error = "unexpected payload layout";
		}
		else
		{
			return true;
		}
		return false;
	}

	bool emit_table(std::ostream *output, std::istream *expected, unsigned char digest[32])
	{
		point_p3 anchor;
		if (!decode_basepoint(&anchor))
		{
			std::cerr << "FIXED16_TABLE_FAIL,cannot decode Ed25519 base point\n";
			return false;
		}
		unsigned char encoded_anchor[32];
		point_to_bytes(encoded_anchor, &anchor);
		if (memcmp(encoded_anchor, compressed_basepoint, 32) != 0)
		{
			std::cerr << "FIXED16_TABLE_FAIL,base point round-trip mismatch\n";
			return false;
		}

		Fixed16Sha256 hash;
		std::vector<point_p3> points(FIXED16_TABLE_VALUES - 1);
		std::vector<std::array<int32_t, 10>> prefixes(FIXED16_TABLE_VALUES - 1);
		std::vector<std::array<int32_t, 10>> inverses(FIXED16_TABLE_VALUES - 1);
		unsigned char serialized[FIXED16_TABLE_ENTRY_SIZE];
		unsigned char expected_bytes[FIXED16_TABLE_ENTRY_SIZE];

		for (uint32_t position = 0; position < FIXED16_TABLE_POSITIONS; ++position)
		{
			auto started = std::chrono::steady_clock::now();
			precomp identity;
			fe_1(identity.yplusx);
			fe_1(identity.yminusx);
			fe_0(identity.xy2d);
			serialize_entry(serialized, identity);
			if (expected != NULL)
			{
				expected->read(reinterpret_cast<char *>(expected_bytes), sizeof(expected_bytes));
				if (!*expected || memcmp(serialized, expected_bytes, sizeof(serialized)) != 0)
				{
					std::cerr << "FIXED16_TABLE_FAIL,mismatch at position=" << position << ",value=0\n";
					return false;
				}
			}
			else
			{
				output->write(reinterpret_cast<const char *>(serialized), sizeof(serialized));
			}
			hash.update(serialized, sizeof(serialized));

			point_cached cached_anchor;
			point_to_cached(&cached_anchor, &anchor);
			points[0] = anchor;
			for (uint32_t value = 1; value + 1 < FIXED16_TABLE_VALUES; ++value)
			{
				point_p1p1 sum;
				point_add(&sum, &points[value - 1], &cached_anchor);
				point_p1p1_to_p3(&points[value], &sum);
			}

			fe_copy(prefixes[0].data(), points[0].Z);
			if (field_is_zero(prefixes[0].data()))
			{
				std::cerr << "FIXED16_TABLE_FAIL,zero Z at position=" << position << ",value=1\n";
				return false;
			}
			for (uint32_t index = 1; index < points.size(); ++index)
			{
				if (field_is_zero(points[index].Z))
				{
					std::cerr << "FIXED16_TABLE_FAIL,zero Z at position=" << position
							  << ",value=" << index + 1 << "\n";
					return false;
				}
				fe_mul(prefixes[index].data(), prefixes[index - 1].data(), points[index].Z);
			}
			fe accumulator;
			fe_invert(accumulator, prefixes.back().data());
			for (size_t index = points.size(); index-- > 1;)
			{
				fe_mul(inverses[index].data(), accumulator, prefixes[index - 1].data());
				fe_mul(accumulator, accumulator, points[index].Z);
			}
			fe_copy(inverses[0].data(), accumulator);

			for (uint32_t index = 0; index < points.size(); ++index)
			{
				precomp entry;
				fe x;
				fe y;
				fe_mul(x, points[index].X, inverses[index].data());
				fe_mul(y, points[index].Y, inverses[index].data());
				fe_add(entry.yplusx, y, x);
				fe_sub(entry.yminusx, y, x);
				fe_mul(entry.xy2d, points[index].T, inverses[index].data());
				fe_mul(entry.xy2d, entry.xy2d, curve_d2);
				canonicalize(entry.yplusx);
				canonicalize(entry.yminusx);
				canonicalize(entry.xy2d);
				serialize_entry(serialized, entry);
				if (expected != NULL)
				{
					expected->read(reinterpret_cast<char *>(expected_bytes), sizeof(expected_bytes));
					if (!*expected || memcmp(serialized, expected_bytes, sizeof(serialized)) != 0)
					{
						std::cerr << "FIXED16_TABLE_FAIL,mismatch at position=" << position
								  << ",value=" << index + 1 << "\n";
						return false;
					}
				}
				else
				{
					output->write(reinterpret_cast<const char *>(serialized), sizeof(serialized));
				}
				hash.update(serialized, sizeof(serialized));
			}

			point_p3 next_anchor = anchor;
			for (int bit = 0; bit < 16; ++bit)
			{
				point_p3 doubled;
				point_double(&doubled, &next_anchor);
				next_anchor = doubled;
			}
			anchor = next_anchor;
			auto elapsed = std::chrono::duration<double>(
							   std::chrono::steady_clock::now() - started)
							   .count();
			std::cout << "FIXED16_TABLE_POSITION," << position << ",seconds=" << elapsed << "\n";
		}

		if (expected != NULL && expected->peek() != std::char_traits<char>::eof())
		{
			std::cerr << "FIXED16_TABLE_FAIL,trailing payload data\n";
			return false;
		}
		if (output != NULL && !*output)
		{
			std::cerr << "FIXED16_TABLE_FAIL,cannot write payload\n";
			return false;
		}
		hash.finish(digest);
		return true;
	}

	int generate(const std::string &path)
	{
		std::string temporary_path = path + ".tmp";
		std::fstream file(temporary_path.c_str(), std::ios::binary | std::ios::in |
													  std::ios::out | std::ios::trunc);
		if (!file)
		{
			std::cerr << "FIXED16_TABLE_FAIL,cannot create " << temporary_path << "\n";
			return 1;
		}
		unsigned char empty_header[FIXED16_TABLE_HEADER_SIZE] = {};
		file.write(reinterpret_cast<const char *>(empty_header), sizeof(empty_header));
		unsigned char digest[32];
		if (!emit_table(&file, NULL, digest))
		{
			file.close();
			remove(temporary_path.c_str());
			return 1;
		}
		unsigned char header[FIXED16_TABLE_HEADER_SIZE];
		make_header(header, digest);
		file.seekp(0);
		file.write(reinterpret_cast<const char *>(header), sizeof(header));
		file.close();
		if (!file)
		{
			std::cerr << "FIXED16_TABLE_FAIL,cannot finalize " << temporary_path << "\n";
			remove(temporary_path.c_str());
			return 1;
		}
		if (rename(temporary_path.c_str(), path.c_str()) != 0)
		{
			std::cerr << "FIXED16_TABLE_FAIL,cannot rename artifact: " << strerror(errno) << "\n";
			remove(temporary_path.c_str());
			return 1;
		}
		std::cout << "FIXED16_TABLE_GENERATED,path=" << path
				  << ",payload_bytes=" << FIXED16_TABLE_PAYLOAD_SIZE
				  << ",sha256=" << fixed16_digest_hex(digest) << "\n";
		return 0;
	}

	int verify(const std::string &path)
	{
		std::ifstream file(path.c_str(), std::ios::binary);
		if (!file)
		{
			std::cerr << "FIXED16_TABLE_FAIL,cannot open " << path << "\n";
			return 1;
		}
		unsigned char header[FIXED16_TABLE_HEADER_SIZE];
		file.read(reinterpret_cast<char *>(header), sizeof(header));
		std::string error;
		if (!file || !validate_header(header, &error))
		{
			std::cerr << "FIXED16_TABLE_FAIL," << error << "\n";
			return 1;
		}
		unsigned char actual_digest[32];
		if (!emit_table(NULL, &file, actual_digest))
		{
			return 1;
		}
		if (memcmp(actual_digest, header + 52, 32) != 0)
		{
			std::cerr << "FIXED16_TABLE_FAIL,payload SHA-256 mismatch\n";
			return 1;
		}
		if (fixed16_digest_hex(actual_digest) != FIXED16_TABLE_SHA256_HEX)
		{
			std::cerr << "FIXED16_TABLE_FAIL,unexpected deterministic artifact SHA-256\n";
			return 1;
		}
		std::cout << "FIXED16_TABLE_VERIFIED,path=" << path
				  << ",payload_bytes=" << FIXED16_TABLE_PAYLOAD_SIZE
				  << ",sha256=" << fixed16_digest_hex(actual_digest) << "\n";
		return 0;
	}

} // namespace

int fixed16_generate_table(const std::string &path)
{
	try
	{
		return generate(path);
	}
	catch (const std::exception &error)
	{
		std::cerr << "FIXED16_TABLE_FAIL," << error.what() << "\n";
		return 1;
	}
}

int fixed16_verify_table(const std::string &path)
{
	try
	{
		return verify(path);
	}
	catch (const std::exception &error)
	{
		std::cerr << "FIXED16_TABLE_FAIL," << error.what() << "\n";
		return 1;
	}
}

bool fixed16_generate_payload(std::vector<unsigned char> &payload)
{
	std::ostringstream output(std::ios::binary);
	unsigned char digest[32];
	if (!emit_table(&output, NULL, digest))
	{
		return false;
	}
	std::string serialized = output.str();
	if (serialized.size() != FIXED16_TABLE_PAYLOAD_SIZE)
	{
		std::cerr << "FIXED16_TABLE_FAIL,unexpected generated size\n";
		return false;
	}
	if (fixed16_digest_hex(digest) != FIXED16_TABLE_SHA256_HEX)
	{
		std::cerr << "FIXED16_TABLE_FAIL,unexpected deterministic artifact SHA-256\n";
		return false;
	}
	payload.assign(serialized.begin(), serialized.end());
	return true;
}