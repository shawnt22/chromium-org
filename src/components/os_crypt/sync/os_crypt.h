// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_OS_CRYPT_SYNC_OS_CRYPT_H_
#define COMPONENTS_OS_CRYPT_SYNC_OS_CRYPT_H_

#include <array>
#include <memory>
#include <optional>
#include <string>

#include "base/component_export.h"
#include "base/functional/callback.h"
#include "base/task/single_thread_task_runner.h"
#include "build/build_config.h"
#include "build/chromecast_buildflags.h"
#include "crypto/subtle_passkey.h"

#if BUILDFLAG(IS_APPLE)
namespace crypto {
class AppleKeychain;
}
#endif

#if BUILDFLAG(IS_LINUX)
class KeyStorageLinux;
#endif  // BUILDFLAG(IS_LINUX)

#if BUILDFLAG(IS_WIN)
class PrefRegistrySimple;
class PrefService;
#endif  // BUILDFLAG(IS_WIN)

namespace os_crypt {
struct Config;
}

// Temporary interface due to OSCrypt refactor. See OSCryptImpl for descriptions
// of what each function does.
namespace OSCrypt {
#if BUILDFLAG(IS_LINUX)
COMPONENT_EXPORT(OS_CRYPT)
void SetConfig(std::unique_ptr<os_crypt::Config> config);
#endif  // BUILDFLAG(IS_LINUX)
COMPONENT_EXPORT(OS_CRYPT) bool IsEncryptionAvailable();
COMPONENT_EXPORT(OS_CRYPT)
bool EncryptString16(const std::u16string& plaintext, std::string* ciphertext);
COMPONENT_EXPORT(OS_CRYPT)
bool DecryptString16(const std::string& ciphertext, std::u16string* plaintext);
COMPONENT_EXPORT(OS_CRYPT)
bool EncryptString(const std::string& plaintext, std::string* ciphertext);
COMPONENT_EXPORT(OS_CRYPT)
bool DecryptString(const std::string& ciphertext, std::string* plaintext);
#if BUILDFLAG(IS_WIN)
COMPONENT_EXPORT(OS_CRYPT)
void RegisterLocalPrefs(PrefRegistrySimple* registry);
COMPONENT_EXPORT(OS_CRYPT) bool Init(PrefService* local_state);

// Initialises OSCryptImpl using an encryption key present in the |local_state|.
// It is similar to the Init() method above, however, it will not create
// a new encryption key if it is not present in the |local_state|.
enum InitResult {
  kSuccess,
  kKeyDoesNotExist,
  kInvalidKeyFormat,
  kDecryptionFailed
};

COMPONENT_EXPORT(OS_CRYPT)
InitResult InitWithExistingKey(PrefService* local_state);
#endif  // BUILDFLAG(IS_WIN)
#if BUILDFLAG(IS_APPLE)
COMPONENT_EXPORT(OS_CRYPT) void UseMockKeychainForTesting(bool use_mock);
COMPONENT_EXPORT(OS_CRYPT)
void UseLockedMockKeychainForTesting(bool use_locked);
#endif  // BUILDFLAG(IS_APPLE)
COMPONENT_EXPORT(OS_CRYPT)
std::string GetRawEncryptionKey();
COMPONENT_EXPORT(OS_CRYPT)
void SetRawEncryptionKey(const std::string& key);
#if BUILDFLAG(IS_WIN)
COMPONENT_EXPORT(OS_CRYPT) void UseMockKeyForTesting(bool use_mock);
COMPONENT_EXPORT(OS_CRYPT) void SetLegacyEncryptionForTesting(bool legacy);
COMPONENT_EXPORT(OS_CRYPT) void ResetStateForTesting();
#endif  // BUILDFLAG(IS_WIN)
#if (BUILDFLAG(IS_LINUX) && !BUILDFLAG(IS_CASTOS))
COMPONENT_EXPORT(OS_CRYPT)
void UseMockKeyStorageForTesting(
    base::OnceCallback<std::unique_ptr<KeyStorageLinux>()>
        storage_provider_factory);
COMPONENT_EXPORT(OS_CRYPT) void ClearCacheForTesting();
COMPONENT_EXPORT(OS_CRYPT)
void SetEncryptionPasswordForTesting(const std::string& password);
#endif  // (BUILDFLAG(IS_LINUX) && !BUILDFLAG(IS_CASTOS))
#if BUILDFLAG(IS_POSIX) && !BUILDFLAG(IS_APPLE) &&         \
        !(BUILDFLAG(IS_LINUX) && !BUILDFLAG(IS_CASTOS)) || \
    BUILDFLAG(IS_FUCHSIA)
COMPONENT_EXPORT(OS_CRYPT)
void SetEncryptionAvailableForTesting(std::optional<bool> available);
#endif  // BUILDFLAG(IS_POSIX) && !BUILDFLAG(IS_APPLE) && !(BUILDFLAG(IS_LINUX)
        // && !BUILDFLAG(IS_CASTOS)) || BUILDFLAG(IS_FUCHSIA)
}  // namespace OSCrypt

// The OSCryptImpl class gives access to simple encryption and decryption of
// strings. Note that on Mac, access to the system Keychain is required and
// these calls can block the current thread to collect user input. The same is
// true for Linux, if a password management tool is available.
class COMPONENT_EXPORT(OS_CRYPT) OSCryptImpl {
 public:
  OSCryptImpl();
  ~OSCryptImpl();
  OSCryptImpl(const OSCryptImpl&) = delete;
  OSCryptImpl(OSCryptImpl&&) = delete;
  OSCryptImpl& operator=(const OSCryptImpl&) = delete;
  OSCryptImpl& operator=(OSCryptImpl&&) = delete;

  // Returns singleton instance of OSCryptImpl.
  static OSCryptImpl* GetInstance();

#if BUILDFLAG(IS_LINUX)
  // Set the configuration of OSCryptImpl.
  // This method, or SetRawEncryptionKey(), must be called before using
  // EncryptString() and DecryptString().
  void SetConfig(std::unique_ptr<os_crypt::Config> config);
#endif  // BUILDFLAG(IS_LINUX)

  // In production code:
  // - On Linux, returns true iff the real secret key (not hardcoded one) is
  //   available.
  // - On MacOS, returns true if Keychain is available (for mock Keychain it
  //   returns true if not using locked Keychain, false if using locked mock
  //   Keychain).
  // - On Windows, returns true if non mock encryption key is available.
  // - On other platforms, returns true as OSCryptImpl will use a hardcoded key.
  //
  // Tests may override the above behavior.
  bool IsEncryptionAvailable();

  // Encrypt a string16. The output (second argument) is really an array of
  // bytes, but we're passing it back as a std::string.
  bool EncryptString16(const std::u16string& plaintext,
                       std::string* ciphertext);

  // Decrypt an array of bytes obtained with EncryptString16 back into a
  // string16. Note that the input (first argument) is a std::string, so you
  // need to first get your (binary) data into a string.
  bool DecryptString16(const std::string& ciphertext,
                       std::u16string* plaintext);

  // Encrypt a string.
  bool EncryptString(const std::string& plaintext, std::string* ciphertext);

  // Decrypt an array of bytes obtained with EnctryptString back into a string.
  // Note that the input (first argument) is a std::string, so you need to first
  // get your (binary) data into a string.
  bool DecryptString(const std::string& ciphertext, std::string* plaintext);

#if BUILDFLAG(IS_WIN)
  // Registers preferences used by OSCryptImpl.
  static void RegisterLocalPrefs(PrefRegistrySimple* registry);

  // Initialises OSCryptImpl.
  // This method should be called on the main UI thread before any calls to
  // encryption or decryption. Returns |true| if os_crypt successfully
  // initialized.
  bool Init(PrefService* local_state);

  // Initialises OSCryptImpl using an encryption key present in the
  // |local_state|. It is similar to the Init() method above, however, it will
  // not create a new encryption key if it is not present in the |local_state|.

  OSCrypt::InitResult InitWithExistingKey(PrefService* local_state);
#endif

#if BUILDFLAG(IS_APPLE)
  // For unit testing purposes we instruct the Encryptor to use a mock Keychain
  // on the Mac. The default is to use the real Keychain. Use OSCryptMocker,
  // instead of calling this method directly.
  void UseMockKeychainForTesting(bool use_mock);

  // When Keychain is locked, it's not possible to get the encryption key. This
  // is used only for testing purposes. Enabling locked Keychain also enables
  // mock Keychain. Use OSCryptMocker, instead of calling this method directly.
  void UseLockedMockKeychainForTesting(bool use_locked);
#endif

  // Get the raw encryption key to be used for all AES encryption. The result
  // can be used to call SetRawEncryptionKey() in another process. Returns an
  // empty string in some situations, for example:
  // - password access is denied
  // - key generation error
  // - if a hardcoded password is used instead of a random per-user key
  // This method is thread-safe.
  std::string GetRawEncryptionKey();

  // Set the raw encryption key to be used for all AES encryption.
  // On platforms that may use a hardcoded key, |key| can be empty and
  // OSCryptImpl will default to the hardcoded key. This method is thread-safe.
  void SetRawEncryptionKey(const std::string& key);

#if BUILDFLAG(IS_WIN)
  // For unit testing purposes we instruct the Encryptor to use a mock Key. The
  // default is to use the real Key bound to profile. Use OSCryptMocker, instead
  // of calling this method directly.
  void UseMockKeyForTesting(bool use_mock);

  // For unit testing purposes, encrypt data using the older DPAPI method rather
  // than using a session key.
  void SetLegacyEncryptionForTesting(bool legacy);

  // For unit testing purposes, reset the state of OSCryptImpl so a new key can
  // be loaded via Init() or SetRawEncryptionkey().
  void ResetStateForTesting();
#endif

#if (BUILDFLAG(IS_LINUX) && !BUILDFLAG(IS_CASTOS))
  // For unit testing purposes, inject methods to be used.
  // |storage_provider_factory| provides the desired |KeyStorage|
  // implementation. If the provider returns |nullptr|, a hardcoded password
  // will be used. If |storage_provider_factory| is null callback, restores the
  // real implementation.
  void UseMockKeyStorageForTesting(
      base::OnceCallback<std::unique_ptr<KeyStorageLinux>()>
          storage_provider_factory);

  // Clears any caching and most lazy initialisations performed by the
  // production code. Should be used after any test which required a password.
  void ClearCacheForTesting();

  // Sets the password with which the encryption key is derived, e.g. "peanuts".
  void SetEncryptionPasswordForTesting(const std::string& password);
#endif  // (BUILDFLAG(IS_LINUX) && !BUILDFLAG(IS_CASTOS))
 private:
#if BUILDFLAG(IS_APPLE)
  // Return the keychain to use for accessing the encryption key.
  std::unique_ptr<crypto::AppleKeychain> GetKeychain() const;

  // Derives an encryption key from data stored in the keychain if necessary.
  // Returns true if there is an encryption key available and false otherwise.
  bool DeriveKey();
#endif  // BUILDFLAG(IS_APPLE)

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_APPLE)
  // This lock is used to make the GetEncryptionKey and
  // GetRawEncryptionKey methods thread-safe.
  static base::Lock& GetLock();
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_APPLE)

#if BUILDFLAG(IS_LINUX)
  static constexpr size_t kDerivedKeyBytes = 16;

  crypto::SubtlePassKey MakeCryptoPassKey();

  // Derive a new key of `kDerivedKeyBytes` from a given input key using
  // PBKDF2-HMAC-SHA1.
  std::array<uint8_t, kDerivedKeyBytes> Pbkdf2(const std::string& key);

  // Try to fill in `v11_key_` with a V1.1 derived key. Returns true if a v11
  // key is now present in `v11_key_` (which may have just been cached
  // previously) and false if one is not present. If `try_v11_` is false, and
  // there is no cached v11 key, this method just returns false.
  bool DeriveV11Key();

  // The cached V1.1 derived key. If this is nullopt, no V1.1 key is available
  // yet, but `DeriveV11Key()` may be able to generate one.
  std::optional<std::array<uint8_t, kDerivedKeyBytes>> v11_key_;

  // Whether to try V1.1 key generation at all. When OSCrypt is used in the
  // network service, V1.1 key generation can't succeed (it is blocked by the
  // sandbox) so it should never be attempted.
  bool try_v11_ = true;

  // |config_| is used to initialise |password_v11_cache_| and then cleared.
  std::unique_ptr<os_crypt::Config> config_;

  base::OnceCallback<std::unique_ptr<KeyStorageLinux>()>
      storage_provider_factory_for_testing_;
#endif  // BUILDFLAG(IS_LINUX)

#if BUILDFLAG(IS_WIN)
  // Use mock key instead of a real encryption key. Used for testing.
  bool use_mock_key_ = false;

  // Store data using the legacy (DPAPI) method rather than session key.
  bool use_legacy_ = false;

  // Encryption Key. Set either by calling Init() or SetRawEncryptionKey().
  std::string encryption_key_;

  // Mock Encryption Key. Only set and used if use_mock_key_ is true.
  std::string mock_encryption_key_;
#endif  // BUILDFLAG(IS_WIN)

#if BUILDFLAG(IS_APPLE)
  // `try_keychain_` indicates whether this object should try using the keychain
  // (which may itself be mocked out) to derive an encryption key; it can be
  // false even if `key_present_` is also false because this object will only
  // try using the keychain at most once and if the first use fails it will
  // persistently fail to decrypt.
  bool try_keychain_ = true;

  static constexpr size_t kDerivedKeySize = 16;
  std::optional<std::array<uint8_t, kDerivedKeySize>> key_;
  // TODO(crbug.com/389737048): Refactor to allow dependency injection of Keychain.
  bool use_mock_keychain_ = false;
  // This flag is used to make the GetEncryptionKey method return NULL if used
  // along with mock Keychain.
  bool use_locked_mock_keychain_ = false;
#endif
};

#endif  // COMPONENTS_OS_CRYPT_SYNC_OS_CRYPT_H_
