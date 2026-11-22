#include "core/version.hpp"

#include <catch2/catch_test_macros.hpp>

// Noms de tests sans accents : CTest les passe en argument à l'exécutable, et
// sous Windows la page de codes les déforme, ce qui ne trouve plus aucun test.
TEST_CASE("la version est celle de la construction")
{
    CHECK(transfertphoto::version() == EXPECTED_VERSION);
}
