#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

#include "fernet.h"
#include "server/runtime_test_utils.h"
#include "token_store.h"

namespace {

constexpr std::string_view kTokenEncryptionKeyHexEnv = "TIGHTROPE_TOKEN_ENCRYPTION_KEY_HEX";
constexpr std::string_view kTokenEncryptionKeyFileEnv = "TIGHTROPE_TOKEN_ENCRYPTION_KEY_FILE";
constexpr std::string_view kTokenEncryptionKeyFilePassphraseEnv = "TIGHTROPE_TOKEN_ENCRYPTION_KEY_FILE_PASSPHRASE";
constexpr std::string_view kRequireEncryptedAtRestEnv = "TIGHTROPE_TOKEN_ENCRYPTION_REQUIRE_ENCRYPTED_AT_REST";
constexpr std::string_view kMigratePlaintextOnReadEnv = "TIGHTROPE_TOKEN_ENCRYPTION_MIGRATE_PLAINTEXT_ON_READ";

} // namespace

TEST_CASE("token store passthrough keeps plaintext when encryption is not configured", "[auth][crypto][token-store]") {
    tightrope::tests::server::EnvVarGuard key_hex_guard{kTokenEncryptionKeyHexEnv.data()};
    tightrope::tests::server::EnvVarGuard key_file_guard{kTokenEncryptionKeyFileEnv.data()};
    tightrope::tests::server::EnvVarGuard key_passphrase_guard{kTokenEncryptionKeyFilePassphraseEnv.data()};
    tightrope::tests::server::EnvVarGuard strict_mode_guard{kRequireEncryptedAtRestEnv.data()};
    tightrope::tests::server::EnvVarGuard migrate_guard{kMigratePlaintextOnReadEnv.data()};
    REQUIRE(key_hex_guard.set(""));
    REQUIRE(key_file_guard.set(""));
    REQUIRE(key_passphrase_guard.set(""));
    REQUIRE(strict_mode_guard.set("0"));
    REQUIRE(migrate_guard.set("1"));
    tightrope::auth::crypto::reset_token_storage_crypto_for_testing();

    const auto stored = tightrope::auth::crypto::encrypt_token_for_storage("plain-access-token");
    REQUIRE(stored.has_value());
    REQUIRE(*stored == "plain-access-token");
    REQUIRE(stored->find(tightrope::auth::crypto::token_storage_cipher_prefix()) != 0);

    const auto loaded = tightrope::auth::crypto::decrypt_token_from_storage(*stored);
    REQUIRE(loaded.has_value());
    REQUIRE(*loaded == "plain-access-token");
}

TEST_CASE("token store encrypts and decrypts when encryption key hex is configured", "[auth][crypto][token-store]") {
    tightrope::tests::server::EnvVarGuard key_hex_guard{kTokenEncryptionKeyHexEnv.data()};
    tightrope::tests::server::EnvVarGuard key_file_guard{kTokenEncryptionKeyFileEnv.data()};
    tightrope::tests::server::EnvVarGuard key_passphrase_guard{kTokenEncryptionKeyFilePassphraseEnv.data()};
    tightrope::tests::server::EnvVarGuard strict_mode_guard{kRequireEncryptedAtRestEnv.data()};
    tightrope::tests::server::EnvVarGuard migrate_guard{kMigratePlaintextOnReadEnv.data()};
    REQUIRE(key_file_guard.set(""));
    REQUIRE(key_passphrase_guard.set(""));
    REQUIRE(strict_mode_guard.set("0"));
    REQUIRE(migrate_guard.set("1"));

    const auto key = tightrope::auth::crypto::generate_secret_key();
    REQUIRE(key.has_value());
    REQUIRE(key_hex_guard.set(tightrope::auth::crypto::key_to_hex(*key)));
    tightrope::auth::crypto::reset_token_storage_crypto_for_testing();

    const auto stored = tightrope::auth::crypto::encrypt_token_for_storage("plain-refresh-token");
    REQUIRE(stored.has_value());
    REQUIRE(stored->starts_with(tightrope::auth::crypto::token_storage_cipher_prefix()));
    REQUIRE(*stored != "plain-refresh-token");

    const auto loaded = tightrope::auth::crypto::decrypt_token_from_storage(*stored);
    REQUIRE(loaded.has_value());
    REQUIRE(*loaded == "plain-refresh-token");
}

TEST_CASE("token store defaults to strict plaintext rejection when key is configured", "[auth][crypto][token-store]") {
    tightrope::tests::server::EnvVarGuard key_hex_guard{kTokenEncryptionKeyHexEnv.data()};
    tightrope::tests::server::EnvVarGuard key_file_guard{kTokenEncryptionKeyFileEnv.data()};
    tightrope::tests::server::EnvVarGuard key_passphrase_guard{kTokenEncryptionKeyFilePassphraseEnv.data()};
    tightrope::tests::server::EnvVarGuard strict_mode_guard{kRequireEncryptedAtRestEnv.data()};
    tightrope::tests::server::EnvVarGuard migrate_guard{kMigratePlaintextOnReadEnv.data()};
    REQUIRE(key_file_guard.set(""));
    REQUIRE(key_passphrase_guard.set(""));
    REQUIRE(strict_mode_guard.set(""));
    REQUIRE(migrate_guard.set("1"));

    const auto key = tightrope::auth::crypto::generate_secret_key();
    REQUIRE(key.has_value());
    REQUIRE(key_hex_guard.set(tightrope::auth::crypto::key_to_hex(*key)));
    tightrope::auth::crypto::reset_token_storage_crypto_for_testing();

    REQUIRE_FALSE(tightrope::auth::crypto::token_storage_plaintext_allowed());

    std::string decrypt_error;
    const auto loaded = tightrope::auth::crypto::decrypt_token_from_storage("legacy-plaintext-token", &decrypt_error);
    REQUIRE_FALSE(loaded.has_value());
    REQUIRE(decrypt_error.find("plaintext token payload is not allowed") != std::string::npos);

    bool migrated = false;
    std::string migrate_error;
    const auto migrated_value = tightrope::auth::crypto::migrate_plaintext_token_for_storage(
        "legacy-plaintext-token",
        &migrated,
        &migrate_error
    );
    REQUIRE(migrated_value.has_value());
    REQUIRE(migrate_error.empty());
    REQUIRE(migrated);
    REQUIRE(migrated_value->starts_with(tightrope::auth::crypto::token_storage_cipher_prefix()));
}

TEST_CASE("token store rejects encrypted payload when key is not configured", "[auth][crypto][token-store]") {
    tightrope::tests::server::EnvVarGuard key_hex_guard{kTokenEncryptionKeyHexEnv.data()};
    tightrope::tests::server::EnvVarGuard key_file_guard{kTokenEncryptionKeyFileEnv.data()};
    tightrope::tests::server::EnvVarGuard key_passphrase_guard{kTokenEncryptionKeyFilePassphraseEnv.data()};
    tightrope::tests::server::EnvVarGuard strict_mode_guard{kRequireEncryptedAtRestEnv.data()};
    tightrope::tests::server::EnvVarGuard migrate_guard{kMigratePlaintextOnReadEnv.data()};
    REQUIRE(key_file_guard.set(""));
    REQUIRE(key_passphrase_guard.set(""));
    REQUIRE(strict_mode_guard.set("0"));
    REQUIRE(migrate_guard.set("1"));

    const auto key = tightrope::auth::crypto::generate_secret_key();
    REQUIRE(key.has_value());
    REQUIRE(key_hex_guard.set(tightrope::auth::crypto::key_to_hex(*key)));
    tightrope::auth::crypto::reset_token_storage_crypto_for_testing();

    const auto stored = tightrope::auth::crypto::encrypt_token_for_storage("plain-id-token");
    REQUIRE(stored.has_value());
    REQUIRE(stored->starts_with(tightrope::auth::crypto::token_storage_cipher_prefix()));

    REQUIRE(key_hex_guard.set(""));
    tightrope::auth::crypto::reset_token_storage_crypto_for_testing();

    std::string error;
    const auto loaded = tightrope::auth::crypto::decrypt_token_from_storage(*stored, &error);
    REQUIRE_FALSE(loaded.has_value());
    REQUIRE(error.find("requires configured token encryption key") != std::string::npos);
}

TEST_CASE(
    "token store strict mode rejects plaintext when encryption key is not configured",
    "[auth][crypto][token-store]"
) {
    tightrope::tests::server::EnvVarGuard key_hex_guard{kTokenEncryptionKeyHexEnv.data()};
    tightrope::tests::server::EnvVarGuard key_file_guard{kTokenEncryptionKeyFileEnv.data()};
    tightrope::tests::server::EnvVarGuard key_passphrase_guard{kTokenEncryptionKeyFilePassphraseEnv.data()};
    tightrope::tests::server::EnvVarGuard strict_mode_guard{kRequireEncryptedAtRestEnv.data()};
    tightrope::tests::server::EnvVarGuard migrate_guard{kMigratePlaintextOnReadEnv.data()};
    REQUIRE(key_hex_guard.set(""));
    REQUIRE(key_file_guard.set(""));
    REQUIRE(key_passphrase_guard.set(""));
    REQUIRE(strict_mode_guard.set("1"));
    REQUIRE(migrate_guard.set("1"));
    tightrope::auth::crypto::reset_token_storage_crypto_for_testing();

    std::string encrypt_error;
    const auto stored = tightrope::auth::crypto::encrypt_token_for_storage("plain-access-token", &encrypt_error);
    REQUIRE_FALSE(stored.has_value());
    REQUIRE(encrypt_error.find("plaintext token storage is disabled") != std::string::npos);

    std::string decrypt_error;
    const auto loaded = tightrope::auth::crypto::decrypt_token_from_storage("plain-access-token", &decrypt_error);
    REQUIRE_FALSE(loaded.has_value());
    REQUIRE(decrypt_error.find("plaintext token payload is not allowed") != std::string::npos);
}

TEST_CASE(
    "token store migrate helper encrypts plaintext when key is configured",
    "[auth][crypto][token-store]"
) {
    tightrope::tests::server::EnvVarGuard key_hex_guard{kTokenEncryptionKeyHexEnv.data()};
    tightrope::tests::server::EnvVarGuard key_file_guard{kTokenEncryptionKeyFileEnv.data()};
    tightrope::tests::server::EnvVarGuard key_passphrase_guard{kTokenEncryptionKeyFilePassphraseEnv.data()};
    tightrope::tests::server::EnvVarGuard strict_mode_guard{kRequireEncryptedAtRestEnv.data()};
    tightrope::tests::server::EnvVarGuard migrate_guard{kMigratePlaintextOnReadEnv.data()};
    REQUIRE(key_file_guard.set(""));
    REQUIRE(key_passphrase_guard.set(""));
    REQUIRE(strict_mode_guard.set("0"));
    REQUIRE(migrate_guard.set("1"));
    const auto key = tightrope::auth::crypto::generate_secret_key();
    REQUIRE(key.has_value());
    REQUIRE(key_hex_guard.set(tightrope::auth::crypto::key_to_hex(*key)));
    tightrope::auth::crypto::reset_token_storage_crypto_for_testing();

    bool migrated = false;
    std::string error;
    const auto migrated_value = tightrope::auth::crypto::migrate_plaintext_token_for_storage(
        "legacy-plaintext-token",
        &migrated,
        &error
    );
    REQUIRE(migrated_value.has_value());
    REQUIRE(migrated);
    REQUIRE(error.empty());
    REQUIRE(migrated_value->starts_with(tightrope::auth::crypto::token_storage_cipher_prefix()));
}
