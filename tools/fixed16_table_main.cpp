#include <cstring>
#include <iostream>
#include <vector>

#include "fixed16_table.h"
#include "fixed16_table_tool.h"

int main(int argc, char **argv)
{
	if (argc == 2 && (std::strcmp(argv[1], "--help") == 0 ||
					  std::strcmp(argv[1], "-h") == 0))
	{
		std::cout << "Usage: " << argv[0] << " generate|verify <table-path>\n";
		return 0;
	}
	if (argc == 2 && std::strcmp(argv[1], "memory-test") == 0)
	{
		std::vector<unsigned char> payload;
		return fixed16_generate_payload(payload) &&
					   payload.size() == FIXED16_TABLE_PAYLOAD_SIZE
				   ? 0
				   : 1;
	}
	if (argc != 3 || (std::strcmp(argv[1], "generate") != 0 &&
					  std::strcmp(argv[1], "verify") != 0))
	{
		std::cerr << "Usage: " << argv[0] << " generate|verify <table-path>\n";
		return 2;
	}
	return std::strcmp(argv[1], "generate") == 0
			   ? fixed16_generate_table(argv[2])
			   : fixed16_verify_table(argv[2]);
}