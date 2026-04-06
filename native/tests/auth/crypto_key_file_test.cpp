#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <optional>
#include <random>
#include <string>

#include "fernet.h"
#include "key_file.h"

namespace {

std::filesystem::path make_temp_key_file_path() {
    static std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<unsigned long long> dist;
    const auto stamp =
        static_cast<unsigned long long>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    return std::filesystem::temp_directory_path() /
           ("tightrope-key-file-" + std::to_string(stamp) + "-" + std::to_string(dist(rng)) + ".key");
}

} // namespace

TEST_CASE("secretbox token round-trip succeeds with same key", "[auth][crypto]") {
    const auto key = tightrope::auth::crypto::generate_secret_key();
    REQUIRE(key.has_value());

    const std::string plaintext = "top-secret-token";
    const auto token = tightrope::auth::crypto::encrypt_token(plaintext, *key);
    REQUIRE(token.has_value());
    REQUIRE_FALSE(token->empty());
    REQUIRE(*token != plaintext);

    const auto decoded = tightrope::auth::crypto::decrypt_token(*token, *key);
    REQUIRE(decoded.has_value());
    REQUIRE(*decoded == plaintext);
}

TEST_CASE("secretbox token decrypt fails for wrong key or tampered token", "[auth][crypto]") {
    const auto key = tightrope::auth::crypto::generate_secret_key();
    const auto wrong_key = tightrope::auth::crypto::generate_secret_key();
    REQUIRE(key.has_value());
    REQUIRE(wrong_key.has_value());

    const auto token = tightrope::auth::crypto::encrypt_token("payload", *key);
    REQUIRE(token.has_value());

    const auto wrong_result = tightrope::auth::crypto::decrypt_token(*token, *wrong_key);
    REQUIRE_FALSE(wrong_result.has_value());

    std::string tampered = *token;
    tampered.front() = (tampered.front() == 'A') ? 'B' : 'A';
    const auto tampered_result = tightrope::auth::crypto::decrypt_token(tampered, *key);
    REQUIRE_FALSE(tampered_result.has_value());
}

TEST_CASE("key file round-trip returns original key", "[auth][crypto]") {
    const auto path = make_temp_key_file_path();
    const auto key = tightrope::auth::crypto::generate_secret_key();
    REQUIRE(key.has_value());

    std::string write_error;
    REQUIRE(tightrope::auth::crypto::write_key_file(path.string(), *key, "correct horse battery staple", &write_error));
    REQUIRE(write_error.empty());

    std::string read_error;
    const auto loaded = tightrope::auth::crypto::read_key_file(path.string(), "correct horse battery staple", &read_error);
    REQUIRE(loaded.has_value());
    REQUIRE(read_error.empty());
    REQUIRE(*loaded == *key);

    std::filesystem::remove(path);
}

TEST_CASE("key file read rejects wrong passphrase", "[auth][crypto]") {
    const auto path = make_temp_key_file_path();
    const auto key = tightrope::auth::crypto::generate_secret_key();
    REQUIRE(key.has_value());
    REQUIRE(tightrope::auth::crypto::write_key_file(path.string(), *key, "right-passphrase"));

    std::string read_error;
    const auto loaded = tightrope::auth::crypto::read_key_file(path.string(), "wrong-passphrase", &read_error);
    REQUIRE_FALSE(loaded.has_value());
    REQUIRE(read_error.find("decrypt key payload") != std::string::npos);

    std::filesystem::remove(path);
}

TEST_CASE("python fernet token decrypt succeeds with valid key", "[auth][crypto]") {
    constexpr std::string_view kSourceKey = "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=";
    constexpr std::string_view kEncryptedToken =
        "gAAAAABlU_EAICEiIyQlJicoKSorLC0uL5q0IHsPKRH7DAZjaDUSJKkhkexd_bmiAUhWKjp2fcL3olidNPZyz9cekXaRZaFw5P5l3PH3OwuV14VCtdAj634=";

    std::string error;
    const auto decrypted =
        tightrope::auth::crypto::decrypt_python_fernet_token(kEncryptedToken, kSourceKey, &error);
    REQUIRE(decrypted.has_value());
    REQUIRE(*decrypted == "plain-text-token");
}

TEST_CASE("python fernet token decrypt fails with wrong key", "[auth][crypto]") {
    constexpr std::string_view kWrongSourceKey = "AQECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=";
    constexpr std::string_view kEncryptedToken =
        "gAAAAABlU_EAICEiIyQlJicoKSorLC0uL5q0IHsPKRH7DAZjaDUSJKkhkexd_bmiAUhWKjp2fcL3olidNPZyz9cekXaRZaFw5P5l3PH3OwuV14VCtdAj634=";

    std::string error;
    const auto decrypted =
        tightrope::auth::crypto::decrypt_python_fernet_token(kEncryptedToken, kWrongSourceKey, &error);
    REQUIRE_FALSE(decrypted.has_value());
    REQUIRE(error.find("authentication failed") != std::string::npos);
}
