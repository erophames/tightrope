#include <catch2/catch_test_macros.hpp>

#include <sodium.h>

#include <string>
#include <string_view>

#include "dashboard/password_auth.h"

TEST_CASE("dashboard password hashing verifies valid credentials", "[auth][dashboard][password]") {
    const auto hash = tightrope::auth::dashboard::hash_password("correct-horse-battery-staple");
    REQUIRE(hash.has_value());
    REQUIRE_FALSE(hash->empty());

    REQUIRE(tightrope::auth::dashboard::verify_password("correct-horse-battery-staple", *hash));
    REQUIRE_FALSE(tightrope::auth::dashboard::verify_password("wrong-password", *hash));
}

TEST_CASE("dashboard password verification rejects malformed hash prefixes", "[auth][dashboard][password]") {
    REQUIRE_FALSE(tightrope::auth::dashboard::verify_password("password", "not-an-argon2-hash"));
    REQUIRE_FALSE(tightrope::auth::dashboard::verify_password("password", "$bcrypt$2b$12$bad"));
}

TEST_CASE("dashboard password verification rejects embedded-null hashes", "[auth][dashboard][password]") {
    std::string malformed = std::string(crypto_pwhash_STRPREFIX) + "v=19$m=65536,t=2,p=1$abc";
    malformed.push_back('\0');
    malformed += "def";

    REQUIRE_FALSE(
        tightrope::auth::dashboard::verify_password(
            "password",
            std::string_view(malformed.data(), malformed.size())
        )
    );
}
