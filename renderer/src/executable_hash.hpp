#pragma once

#include <string>

namespace aitd4 {

// Returns the uppercase hexadecimal SHA-256 of a file, or an empty string when
// the file cannot be opened or hashed. Used to gate address-sensitive hooks to
// the verified executables.
std::string sha256_file(const char* path);

}  // namespace aitd4
