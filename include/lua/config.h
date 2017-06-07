#ifndef LUACONFIG_H
#define LUACONFIG_H

#include <string>

namespace lua
{
namespace config
{

// helper functions for global lua config.
// if multiple values are needed, or if anything
// fancy is needed, prefer directly accessing
// config over calling these helpers

int get_int(const char *name, int def);
float get_float(const char *name, float def);
bool get_bool(const char *name, bool def);
std::string get_string(const char *name);

} // namespace config
} // namespace lua

#endif // LUACONFIG_H
