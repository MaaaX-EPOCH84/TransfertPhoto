#include "core/version.hpp"

#include <cstdlib>
#include <iostream>

int main()
{
    const auto actual = transfertphoto::version();
    if (actual != EXPECTED_VERSION) {
        std::cerr << "version(): got \"" << actual << "\", expected \"" EXPECTED_VERSION "\"\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
