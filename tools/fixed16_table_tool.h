#ifndef TOOLS_FIXED16_TABLE_TOOL_H
#define TOOLS_FIXED16_TABLE_TOOL_H

#include <string>
#include <vector>

int fixed16_generate_table(const std::string &path);
int fixed16_verify_table(const std::string &path);
bool fixed16_generate_payload(std::vector<unsigned char> &payload);

#endif
