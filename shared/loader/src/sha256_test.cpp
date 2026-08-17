#include "sha256.hpp"

#include <cstdio>
#include <cstring>

namespace {

bool check(const char* input, const char* expected) {
    aitdtnn::loader::Sha256 sha;
    sha.update(input, std::strlen(input));
    unsigned char digest[32]{};
    sha.finish(digest);

    char actual[65]{};
    for (std::size_t index = 0; index < 32; ++index) {
        sprintf_s(actual + index * 2, sizeof(actual) - index * 2,
                  "%02x", digest[index]);
    }
    if (std::strcmp(actual, expected) != 0) {
        std::fprintf(stderr, "SHA-256 mismatch for '%s': %s\n", input, actual);
        return false;
    }
    return true;
}

}  // namespace

int main() {
    if (!check("", "e3b0c44298fc1c149afbf4c8996fb924"
                   "27ae41e4649b934ca495991b7852b855") ||
        !check("abc", "ba7816bf8f01cfea414140de5dae2223"
                      "b00361a396177a9cb410ff61f20015ad") ||
        !check("The quick brown fox jumps over the lazy dog",
               "d7a8fbb307d7809469ca9abcb0082e4f"
               "8d5651e46d3cdb762d02d0bf37c9e592")) {
        return 1;
    }
    std::puts("SHA-256 self-test passed (3 vectors).");
    return 0;
}
