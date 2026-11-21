#include "core/version.hpp"

#include <cstdlib>
#include <iostream>

int main()
{
    const auto actual = transfertphoto::version();
    if (actual != EXPECTED_VERSION) {
        std::cerr << "version() : obtenu \"" << actual << "\", attendu \"" EXPECTED_VERSION "\"\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
