// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/variations/variations_seed_store.h"

#include <stdint.h>

#include <utility>

#include "base/base64.h"
#include "base/build_time.h"
#include "base/command_line.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "base/numerics/safe_math.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/task_runner.h"
#include "base/task/thread_pool.h"
#include "base/version_info/channel.h"
#include "build/build_config.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/variations/client_filterable_state.h"
#include "components/variations/pref_names.h"
#include "components/variations/proto/variations_seed.pb.h"
#include "components/variations/seed_reader_writer.h"
#include "components/variations/variations_safe_seed_store_local_state.h"
#include "components/variations/variations_switches.h"
#include "components/version_info/version_info.h"
#include "crypto/signature_verifier.h"
#include "third_party/protobuf/src/google/protobuf/io/coded_stream.h"
#include "third_party/zlib/google/compression_utils.h"

#if BUILDFLAG(IS_ANDROID)
#include "components/variations/android/variations_seed_bridge.h"
#include "components/variations/metrics.h"
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_CHROMEOS)
#include "base/functional/callback.h"
#include "chromeos/ash/components/dbus/featured/featured.pb.h"
#include "chromeos/ash/components/dbus/featured/featured_client.h"
#endif  // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_IOS)
#include "components/variations/metrics.h"
#endif  // BUILDFLAG(IS_IOS)

namespace variations {
namespace {

// The ECDSA public key of the variations server for verifying variations seed
// signatures.
const uint8_t kPublicKey[] = {
    0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02,
    0x01, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03,
    0x42, 0x00, 0x04, 0x51, 0x7c, 0x31, 0x4b, 0x50, 0x42, 0xdd, 0x59, 0xda,
    0x0b, 0xfa, 0x43, 0x44, 0x33, 0x7c, 0x5f, 0xa1, 0x0b, 0xd5, 0x82, 0xf6,
    0xac, 0x04, 0x19, 0x72, 0x6c, 0x40, 0xd4, 0x3e, 0x56, 0xe2, 0xa0, 0x80,
    0xa0, 0x41, 0xb3, 0x23, 0x7b, 0x71, 0xc9, 0x80, 0x87, 0xde, 0x35, 0x0d,
    0x25, 0x71, 0x09, 0x7f, 0xb4, 0x15, 0x2b, 0xff, 0x82, 0x4d, 0xd3, 0xfe,
    0xc5, 0xef, 0x20, 0xc6, 0xa3, 0x10, 0xbf,
};

// A sentinel value that may be stored as the latest variations seed value in
// prefs to indicate that the latest seed is identical to the safe seed. Used to
// avoid duplicating storage space.
constexpr char kIdenticalToSafeSeedSentinel[] = "safe_seed_content";

// The maximum size of an uncompressed seed at 50 MiB.
constexpr std::size_t kMaxUncompressedSeedSize = 50 * 1024 * 1024;

#if BUILDFLAG(IS_CHROMEOS)
// Number of attempts to send the safe seed from Chrome to CrOS platforms before
// giving up.
constexpr int kSendPlatformSafeSeedMaxAttempts = 2;
#endif  // BUILDFLAG(IS_CHROMEOS)

// LINT.IfChange
// The name of the seed file that stores the latest seed data.
const base::FilePath::CharType kSeedFilename[] =
    FILE_PATH_LITERAL("VariationsSeedV1");
// LINT.ThenChange(/testing/scripts/variations_seed_access_helper.py, /components/variations/variations_seed_store.cc, /components/variations/service/variations_field_trial_creator_unittest.cc, /chrome/browser/metrics/variations/variations_safe_mode_end_to_end_browsertest.cc)

// Returns true if |signature| is empty and if the command-line flag to accept
// empty seed signature is specified.
bool AcceptEmptySeedSignatureForTesting(const std::string& signature) {
  return signature.empty() &&
         base::CommandLine::ForCurrentProcess()->HasSwitch(
             switches::kAcceptEmptySeedSignatureForTesting);
}

// Verifies a variations seed (the serialized proto bytes) with the specified
// base-64 encoded signature that was received from the server and returns the
// result. The signature is assumed to be an "ECDSA with SHA-256" signature
// (see kECDSAWithSHA256AlgorithmID in the .cc file). Returns the result of
// signature verification.
VerifySignatureResult VerifySeedSignature(
    const std::string& seed_bytes,
    const std::string& base64_seed_signature) {
  if (base64_seed_signature.empty())
    return VerifySignatureResult::MISSING_SIGNATURE;

  std::string signature;
  if (!base::Base64Decode(base64_seed_signature, &signature))
    return VerifySignatureResult::DECODE_FAILED;

  crypto::SignatureVerifier verifier;
  if (!verifier.VerifyInit(crypto::SignatureVerifier::ECDSA_SHA256,
                           base::as_byte_span(signature), kPublicKey)) {
    return VerifySignatureResult::INVALID_SIGNATURE;
  }

  verifier.VerifyUpdate(base::as_byte_span(seed_bytes));
  if (!verifier.VerifyFinal())
    return VerifySignatureResult::INVALID_SEED;

  return VerifySignatureResult::VALID_SIGNATURE;
}

// Truncates a time to the start of the day in UTC. If given a time representing
// 2014-03-11 10:18:03.1 UTC, it will return a time representing
// 2014-03-11 00:00:00.0 UTC.
base::Time TruncateToUTCDay(base::Time time) {
  base::Time::Exploded exploded;
  time.UTCExplode(&exploded);
  exploded.hour = 0;
  exploded.minute = 0;
  exploded.second = 0;
  exploded.millisecond = 0;

  base::Time out_time;
  bool conversion_success = base::Time::FromUTCExploded(exploded, &out_time);
  DCHECK(conversion_success);
  return out_time;
}

UpdateSeedDateResult GetSeedDateChangeState(
    base::Time server_seed_date,
    base::Time stored_seed_date) {
  if (server_seed_date < stored_seed_date)
    return UpdateSeedDateResult::NEW_DATE_IS_OLDER;

  if (TruncateToUTCDay(server_seed_date) !=
      TruncateToUTCDay(stored_seed_date)) {
    // The server date is later than the stored date, and they are from
    // different UTC days, so |server_seed_date| is a valid new day.
    return UpdateSeedDateResult::NEW_DAY;
  }
  return UpdateSeedDateResult::SAME_DAY;
}

// Remove gzip compression from |data|.
// Returns success or error, populating result on success.
StoreSeedResult Uncompress(const std::string& compressed, std::string* result) {
  DCHECK(result);
  if (!compression::GzipUncompress(compressed, result))
    return StoreSeedResult::kFailedUngzip;
  if (result->empty())
    return StoreSeedResult::kFailedEmptyGzipContents;
  return StoreSeedResult::kSuccess;
}

}  // namespace

ValidatedSeed::ValidatedSeed() = default;
ValidatedSeed::~ValidatedSeed() = default;
ValidatedSeed::ValidatedSeed(ValidatedSeed&& other) = default;
ValidatedSeed& ValidatedSeed::operator=(ValidatedSeed&& other) = default;

bool ValidatedSeed::MatchesStoredSeed(const StoredSeed& stored_seed) const {
  switch (stored_seed.storage_format) {
    case StoredSeed::StorageFormat::kCompressed:
      return compressed_seed_data == stored_seed.data;
    case StoredSeed::StorageFormat::kCompressedAndBase64Encoded:
      return base64_seed_data == stored_seed.data;
  }
}

VariationsSeedStore::VariationsSeedStore(
    PrefService* local_state,
    std::unique_ptr<SeedResponse> initial_seed,
    bool signature_verification_enabled,
    std::unique_ptr<VariationsSafeSeedStore> safe_seed_store,
    version_info::Channel channel,
    const base::FilePath& seed_file_dir,
    const EntropyProviders* entropy_providers,
    bool use_first_run_prefs)
    : local_state_(local_state),
      safe_seed_store_(std::move(safe_seed_store)),
      signature_verification_enabled_(signature_verification_enabled),
      use_first_run_prefs_(use_first_run_prefs),
      seed_reader_writer_(
          std::make_unique<SeedReaderWriter>(local_state,
                                             seed_file_dir,
                                             kSeedFilename,
                                             kRegularSeedFieldsPrefs,
                                             channel,
                                             entropy_providers)) {
#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS)
  if (initial_seed)
    ImportInitialSeed(std::move(initial_seed));
#endif  // BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS)
}

VariationsSeedStore::~VariationsSeedStore() = default;

bool VariationsSeedStore::LoadSeed(VariationsSeed* seed,
                                   std::string* seed_data,
                                   std::string* base64_seed_signature) {
  LoadSeedResult result =
      LoadSeedImpl(SeedType::LATEST, seed, seed_data, base64_seed_signature);
  RecordLoadSeedResult(result);
  if (result != LoadSeedResult::kSuccess)
    return false;

  latest_serial_number_ = seed->serial_number();
  return true;
}

void VariationsSeedStore::StoreSeedData(
    std::string data,
    std::string base64_seed_signature,
    std::string country_code,
    base::Time date_fetched,
    bool is_delta_compressed,
    bool is_gzip_compressed,
    base::OnceCallback<void(bool, VariationsSeed)> done_callback,
    bool require_synchronous) {
  SCOPED_UMA_HISTOGRAM_TIMER("Variations.StoreSeed.Time");

  UMA_HISTOGRAM_COUNTS_1000("Variations.StoreSeed.DataSize",
                            data.length() / 1024);
  InstanceManipulations im = {
      .gzip_compressed = is_gzip_compressed,
      .delta_compressed = is_delta_compressed,
  };
  RecordSeedInstanceManipulations(im);

  // Note: SeedData is move-only, so it will be moved into a param below.
  SeedData seed_data;
  seed_data.data = std::move(data);
  seed_data.base64_seed_signature = std::move(base64_seed_signature);
  seed_data.country_code = std::move(country_code);
  seed_data.date_fetched = date_fetched;
  seed_data.is_gzip_compressed = is_gzip_compressed;
  seed_data.is_delta_compressed = is_delta_compressed;

  if (is_delta_compressed) {
    LoadSeedResult read_result =
        ReadSeedData(SeedType::LATEST, &seed_data.existing_seed_bytes);
    if (read_result != LoadSeedResult::kSuccess) {
      RecordStoreSeedResult(StoreSeedResult::kFailedDeltaReadSeed);
      std::move(done_callback).Run(false, VariationsSeed());
      return;
    }
  }

  if (require_synchronous) {
    SeedProcessingResult result =
        ProcessSeedData(signature_verification_enabled_, std::move(seed_data));
    OnSeedDataProcessed(std::move(done_callback), std::move(result));
  } else {
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::TaskPriority::BEST_EFFORT},
        base::BindOnce(&VariationsSeedStore::ProcessSeedData,
                       signature_verification_enabled_, std::move(seed_data)),
        base::BindOnce(&VariationsSeedStore::OnSeedDataProcessed,
                       weak_ptr_factory_.GetWeakPtr(),
                       std::move(done_callback)));
  }
}

bool VariationsSeedStore::LoadSafeSeed(VariationsSeed* seed,
                                       ClientFilterableState* client_state) {
  std::string unused_seed_data;
  std::string unused_base64_seed_signature;
  LoadSeedResult result = LoadSeedImpl(SeedType::SAFE, seed, &unused_seed_data,
                                       &unused_base64_seed_signature);
  RecordLoadSafeSeedResult(result);
  if (result != LoadSeedResult::kSuccess)
    return false;

  // TODO(crbug.com/40202311): While it's not immediately obvious,
  // |client_state| is not used for successfully loaded safe seeds that are
  // rejected after additional validation (expiry and future milestone).
  client_state->reference_date =
      GetTimeForStudyDateChecks(/*is_safe_seed=*/true);
  client_state->locale = safe_seed_store_->GetLocale();
  client_state->permanent_consistency_country =
      safe_seed_store_->GetPermanentConsistencyCountry();
  client_state->session_consistency_country =
      safe_seed_store_->GetSessionConsistencyCountry();
  return true;
}

bool VariationsSeedStore::StoreSafeSeed(
    const std::string& seed_data,
    const std::string& base64_seed_signature,
    int seed_milestone,
    const ClientFilterableState& client_state,
    base::Time seed_fetch_time) {
  ValidatedSeed seed;
  // TODO(crbug.com/40839193): See if we can avoid calling this on the UI
  // thread.
  StoreSeedResult validation_result =
      ValidateSeedBytes(seed_data, base64_seed_signature, SeedType::SAFE,
                        signature_verification_enabled_, &seed);
  if (validation_result != StoreSeedResult::kSuccess) {
    RecordStoreSafeSeedResult(validation_result);
    return false;
  }

  StoreValidatedSafeSeed(seed, seed_milestone, client_state, seed_fetch_time);
  RecordStoreSafeSeedResult(StoreSeedResult::kSuccess);
  return true;
}

base::Time VariationsSeedStore::GetLatestSeedFetchTime() const {
  return seed_reader_writer_->GetSeedData().client_fetch_time;
}

base::Time VariationsSeedStore::GetSafeSeedFetchTime() const {
  return safe_seed_store_->GetFetchTime();
}

int VariationsSeedStore::GetLatestMilestone() const {
  return seed_reader_writer_->GetSeedData().milestone;
}

int VariationsSeedStore::GetSafeSeedMilestone() const {
  return safe_seed_store_->GetMilestone();
}

base::Time VariationsSeedStore::GetLatestTimeForStudyDateChecks()
    const {
  return seed_reader_writer_->GetSeedData().seed_date;
}

base::Time VariationsSeedStore::GetSafeSeedTimeForStudyDateChecks() const {
  return safe_seed_store_->GetTimeForStudyDateChecks();
}

base::Time VariationsSeedStore::GetTimeForStudyDateChecks(bool is_safe_seed) {
  base::Time seed_date = is_safe_seed ? GetSafeSeedTimeForStudyDateChecks()
                                      : GetLatestTimeForStudyDateChecks();
  const base::Time build_time = base::GetBuildTime();

  // Use the build time for date checks if either the seed date is unknown or
  // the build time is newer than the seed date.
  if (seed_date.is_null() || seed_date < build_time) {
    return build_time;
  }
  return seed_date;
}

void VariationsSeedStore::RecordLastFetchTime(base::Time fetch_time) {
  CHECK(!fetch_time.is_null()) << "Can't record null fetch time.";
  seed_reader_writer_->SetFetchTime(fetch_time);
  // If the latest and safe seeds are identical, update the fetch time for the
  // safe seed as well.
  if (seed_reader_writer_->GetSeedData().data == kIdenticalToSafeSeedSentinel) {
    safe_seed_store_->SetFetchTime(fetch_time);
  }
}

void VariationsSeedStore::UpdateSeedDateAndLogDayChange(
    base::Time server_date_fetched) {
  LogSeedDayChange(server_date_fetched);
  seed_reader_writer_->SetSeedDate(server_date_fetched);
}

void VariationsSeedStore::LogSeedDayChange(
    base::Time server_date_fetched) {
  UpdateSeedDateResult result = UpdateSeedDateResult::NO_OLD_DATE;
  const base::Time stored_date = seed_reader_writer_->GetSeedData().seed_date;
  if (!stored_date.is_null()) {
    result = GetSeedDateChangeState(server_date_fetched, stored_date);
  }

  UMA_HISTOGRAM_ENUMERATION("Variations.SeedDateChange", result,
                            UpdateSeedDateResult::ENUM_SIZE);
}

const std::string& VariationsSeedStore::GetLatestSerialNumber() {
  if (latest_serial_number_.empty()) {
    // Efficiency note: This code should rarely be reached; typically, the
    // latest serial number should be cached via the call to LoadSeed(). The
    // call to ParseFromString() can be expensive, so it's best to only perform
    // it once, if possible: [ https://crbug.com/761684#c2 ]. At the time of
    // this writing, the only expected code path that should reach this code is
    // when running in safe mode.
    std::string seed_data;
    VariationsSeed seed;
    if (ReadSeedData(SeedType::LATEST, &seed_data) ==
            LoadSeedResult::kSuccess &&
        seed.ParseFromString(seed_data)) {
      latest_serial_number_ = seed.serial_number();
    }
  }
  return latest_serial_number_;
}

// static
void VariationsSeedStore::RegisterPrefs(PrefRegistrySimple* registry) {
  // Regular seed prefs:
  registry->RegisterStringPref(prefs::kVariationsCompressedSeed, std::string());
  registry->RegisterStringPref(prefs::kVariationsCountry, std::string());
  registry->RegisterTimePref(prefs::kVariationsLastFetchTime, base::Time());
  registry->RegisterIntegerPref(prefs::kVariationsSeedMilestone, 0);
  registry->RegisterTimePref(prefs::kVariationsSeedDate, base::Time());
  registry->RegisterStringPref(prefs::kVariationsSeedSignature, std::string());

  VariationsSafeSeedStoreLocalState::RegisterPrefs(registry);
}

// static
VerifySignatureResult VariationsSeedStore::VerifySeedSignatureForTesting(
    const std::string& seed_bytes,
    const std::string& base64_seed_signature) {
  return VerifySeedSignature(seed_bytes, base64_seed_signature);
}

VariationsSeedStore::SeedData::SeedData() = default;
VariationsSeedStore::SeedData::~SeedData() = default;
VariationsSeedStore::SeedData::SeedData(VariationsSeedStore::SeedData&& other) =
    default;
VariationsSeedStore::SeedData& VariationsSeedStore::SeedData::operator=(
    VariationsSeedStore::SeedData&& other) = default;

VariationsSeedStore::SeedProcessingResult::SeedProcessingResult(
    SeedData seed_data,
    StoreSeedResult result)
    : seed_data(std::move(seed_data)), result(result) {}
VariationsSeedStore::SeedProcessingResult::~SeedProcessingResult() = default;
VariationsSeedStore::SeedProcessingResult::SeedProcessingResult(
    VariationsSeedStore::SeedProcessingResult&& other) = default;
VariationsSeedStore::SeedProcessingResult&
VariationsSeedStore::SeedProcessingResult::operator=(
    VariationsSeedStore::SeedProcessingResult&& other) = default;

// It is intentional that country-related prefs are retained for regular seeds
// and cleared for safe seeds.
//
// For regular seeds, the prefs are kept for two reasons. First, it's better to
// have some idea of a country recently associated with the device. Second, some
// past, country-gated launches started relying on the VariationsService-
// provided country when they retired server-side configs.
//
// The safe seed prefs are needed to correctly apply a safe seed, so if the safe
// seed is cleared, there's no reason to retain them as they may be incorrect
// for the next safe seed.
void VariationsSeedStore::ClearPrefs(SeedType seed_type) {
  if (seed_type == SeedType::LATEST) {
    // Seed and other related information is cleared by the SeedReaderWriter.
    seed_reader_writer_->ClearSeedInfo();
    return;
  }

  DCHECK_EQ(seed_type, SeedType::SAFE);
  safe_seed_store_->ClearState();
}

#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS)
void VariationsSeedStore::ImportInitialSeed(
    std::unique_ptr<SeedResponse> initial_seed) {
  if (initial_seed->data.empty()) {
    // Note: This is an expected case on non-first run starts.
    RecordFirstRunSeedImportResult(
        FirstRunSeedImportResult::kFailNoFirstRunSeed);
    return;
  }

  if (initial_seed->date.is_null()) {
    RecordFirstRunSeedImportResult(
        FirstRunSeedImportResult::kFailInvalidResponseDate);
    LOG(WARNING) << "Missing response date";
    return;
  }

  auto done_callback =
      base::BindOnce([](bool store_success, VariationsSeed seed) {
        if (store_success) {
          RecordFirstRunSeedImportResult(FirstRunSeedImportResult::kSuccess);
        } else {
          RecordFirstRunSeedImportResult(
              FirstRunSeedImportResult::kFailStoreFailed);
          LOG(WARNING) << "First run variations seed is invalid.";
        }
      });
  StoreSeedData(std::move(initial_seed->data),
                std::move(initial_seed->signature),
                std::move(initial_seed->country), initial_seed->date,
                /*is_delta_compressed=*/false, initial_seed->is_gzip_compressed,
                std::move(done_callback),
                /*require_synchronous=*/true);
}
#endif  // BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS)

// static
std::optional<std::string> VariationsSeedStore::SeedBytesToCompressedBase64Seed(
    const std::string& seed_bytes) {
  if (seed_bytes.empty()) {
    return std::nullopt;
  }

  std::string compressed_seed_data;
  if (!compression::GzipCompress(seed_bytes, &compressed_seed_data)) {
    return std::nullopt;
  }

  return base::Base64Encode(compressed_seed_data);
}

SeedReaderWriter* VariationsSeedStore::GetSeedReaderWriterForTesting() {
  return seed_reader_writer_.get();
}

void VariationsSeedStore::SetSeedReaderWriterForTesting(
    std::unique_ptr<SeedReaderWriter> seed_reader_writer) {
  seed_reader_writer_ = std::move(seed_reader_writer);
}

SeedReaderWriter* VariationsSeedStore::GetSafeSeedReaderWriterForTesting() {
  return safe_seed_store_->GetSeedReaderWriterForTesting();  // IN-TEST
}

void VariationsSeedStore::SetSafeSeedReaderWriterForTesting(
    std::unique_ptr<SeedReaderWriter> seed_reader_writer) {
  safe_seed_store_->SetSeedReaderWriterForTesting(  // IN-TEST
      std::move(seed_reader_writer));
}

LoadSeedResult VariationsSeedStore::VerifyAndParseSeed(
    VariationsSeed* seed,
    const std::string& seed_data,
    const std::string& base64_seed_signature,
    std::optional<VerifySignatureResult>* verify_signature_result) {
  // TODO(crbug.com/40228403): get rid of |signature_verification_enabled_| and
  // only support switches::kAcceptEmptySeedSignatureForTesting.
  if (signature_verification_enabled_ &&
      !AcceptEmptySeedSignatureForTesting(base64_seed_signature)) {
    *verify_signature_result =
        VerifySeedSignature(seed_data, base64_seed_signature);
    if (*verify_signature_result != VerifySignatureResult::VALID_SIGNATURE) {
      return LoadSeedResult::kInvalidSignature;
    }
  }

  if (!seed->ParseFromString(seed_data)) {
    return LoadSeedResult::kCorruptProtobuf;
  }

  return LoadSeedResult::kSuccess;
}

LoadSeedResult VariationsSeedStore::LoadSeedImpl(
    SeedType seed_type,
    VariationsSeed* seed,
    std::string* seed_data,
    std::string* base64_seed_signature) {
  LoadSeedResult read_result =
      ReadSeedData(seed_type, seed_data, base64_seed_signature);
  if (read_result != LoadSeedResult::kSuccess) {
    return read_result;
  }

  std::optional<VerifySignatureResult> verify_signature_result;
  LoadSeedResult result = VerifyAndParseSeed(
      seed, *seed_data, *base64_seed_signature, &verify_signature_result);
  if (verify_signature_result.has_value()) {
    VerifySignatureResult signature_result = verify_signature_result.value();
    if (seed_type == SeedType::LATEST) {
      UMA_HISTOGRAM_ENUMERATION("Variations.LoadSeedSignature",
                                signature_result,
                                VerifySignatureResult::ENUM_SIZE);
    } else {
      UMA_HISTOGRAM_ENUMERATION(
          "Variations.SafeMode.LoadSafeSeed.SignatureValidity",
          signature_result, VerifySignatureResult::ENUM_SIZE);
    }
    if (signature_result != VerifySignatureResult::VALID_SIGNATURE) {
      ClearPrefs(seed_type);
    }
  }

  if (result == LoadSeedResult::kCorruptProtobuf) {
    ClearPrefs(seed_type);
  }

  return result;
}

LoadSeedResult VariationsSeedStore::ReadSeedData(
    SeedType seed_type,
    std::string* seed_data,
    std::string* base64_seed_signature) {
  const StoredSeed loaded_seed = seed_type == SeedType::LATEST
                                     ? seed_reader_writer_->GetSeedData()
                                     : safe_seed_store_->GetCompressedSeed();

  if (loaded_seed.data.empty()) {
    return LoadSeedResult::kEmpty;
  }

  // As a space optimization, the latest seed might not be stored directly, but
  // rather aliased to the safe seed.
  if (seed_type == SeedType::LATEST &&
      loaded_seed.data == kIdenticalToSafeSeedSentinel) {
    return ReadSeedData(SeedType::SAFE, seed_data, base64_seed_signature);
  }

  // If the decode process fails, assume the pref value is corrupt and clear it.
  std::string_view compressed_data;
  std::string decoded_data;
  switch (loaded_seed.storage_format) {
    case StoredSeed::StorageFormat::kCompressed:
      compressed_data = loaded_seed.data;
      break;
    // Because clients not using a seed file get seed data from local state
    // instead, they need to decode the base64-encoded seed data first.
    case StoredSeed::StorageFormat::kCompressedAndBase64Encoded:
      if (!base::Base64Decode(loaded_seed.data, &decoded_data)) {
        ClearPrefs(seed_type);
        return LoadSeedResult::kCorruptBase64;
      }
      compressed_data = decoded_data;
      break;
  }
  // A corrupt seed could result in a very large buffer being allocated which
  // could crash the process.
  if (compression::GetUncompressedSize(compressed_data) >
      kMaxUncompressedSeedSize) {
    ClearPrefs(seed_type);
    return LoadSeedResult::kExceedsUncompressedSizeLimit;
  }
  if (!compression::GzipUncompress(compressed_data, seed_data)) {
    ClearPrefs(seed_type);
    return LoadSeedResult::kCorruptGzip;
  }

  // Copy the signature from the loaded seed.
  if (base64_seed_signature) {
    *base64_seed_signature = loaded_seed.signature;
  }

  return LoadSeedResult::kSuccess;
}

StoreSeedResult VariationsSeedStore::ResolveDelta(
    const std::string& delta_bytes,
    std::string* seed_bytes) {
  DCHECK(seed_bytes);
  std::string existing_seed_bytes;
  LoadSeedResult read_result =
      ReadSeedData(SeedType::LATEST, &existing_seed_bytes);
  if (read_result != LoadSeedResult::kSuccess)
    return StoreSeedResult::kFailedDeltaReadSeed;
  if (!ApplyDeltaPatch(existing_seed_bytes, delta_bytes, seed_bytes))
    return StoreSeedResult::kFailedDeltaApply;
  return StoreSeedResult::kSuccess;
}

StoreSeedResult VariationsSeedStore::ResolveInstanceManipulations(
    const std::string& data,
    const InstanceManipulations& im,
    std::string* seed_bytes) {
  DCHECK(seed_bytes);
  // If the data is gzip compressed, first uncompress it.
  std::string ungzipped_data;
  if (im.gzip_compressed) {
    StoreSeedResult result = Uncompress(data, &ungzipped_data);
    if (result != StoreSeedResult::kSuccess)
      return result;
  } else {
    ungzipped_data = data;
  }

  if (!im.delta_compressed) {
    seed_bytes->swap(ungzipped_data);
    return StoreSeedResult::kSuccess;
  }

  return ResolveDelta(ungzipped_data, seed_bytes);
}

void VariationsSeedStore::OnSeedDataProcessed(
    base::OnceCallback<void(bool, VariationsSeed)> done_callback,
    SeedProcessingResult result) {
  if (result.result != StoreSeedResult::kSuccess) {
    RecordStoreSeedResult(result.result);
    std::move(done_callback).Run(false, VariationsSeed());
    return;
  }

  if (result.validate_result != StoreSeedResult::kSuccess) {
    RecordStoreSeedResult(result.validate_result);
    if (result.seed_data.is_delta_compressed)
      RecordStoreSeedResult(StoreSeedResult::kFailedDeltaStore);
    std::move(done_callback).Run(false, VariationsSeed());
    return;
  }

  StoreValidatedSeed(result.validated, result.seed_data.country_code,
                     result.seed_data.date_fetched);
  RecordStoreSeedResult(StoreSeedResult::kSuccess);
  std::move(done_callback).Run(true, std::move(result.validated.parsed));
}

void VariationsSeedStore::StoreValidatedSeed(const ValidatedSeed& seed,
                                             const std::string& country_code,
                                             base::Time date_fetched) {
#if BUILDFLAG(IS_ANDROID)
  // If currently we do not have any stored seed, then we mark seed storing as
  // successful on the Java side to avoid repeated seed fetches.
  if (use_first_run_prefs_ && seed_reader_writer_->GetSeedData().data.empty()) {
    android::MarkVariationsSeedAsStored();
  }
#endif

  // Update the saved country code only if one was returned from the server.
  if (!country_code.empty())
    local_state_->SetString(prefs::kVariationsCountry, country_code);

  int milestone = version_info::GetMajorVersionNumberAsInt();

  LogSeedDayChange(date_fetched);

  // As a space optimization, store an alias to the safe seed if the contents
  // are identical.
  if (seed.MatchesStoredSeed(safe_seed_store_->GetCompressedSeed())) {
    seed_reader_writer_->StoreValidatedSeedInfo(ValidatedSeedInfo{
        .compressed_seed_data = kIdenticalToSafeSeedSentinel,
        .base64_seed_data = kIdenticalToSafeSeedSentinel,
        .signature = seed.base64_seed_signature,
        .milestone = milestone,
        .seed_date = date_fetched,
        .client_fetch_time = base::Time::Now(),
    });
  } else {
    seed_reader_writer_->StoreValidatedSeedInfo(ValidatedSeedInfo{
        .compressed_seed_data = seed.compressed_seed_data,
        .base64_seed_data = seed.base64_seed_data,
        .signature = seed.base64_seed_signature,
        .milestone = milestone,
        .seed_date = date_fetched,
        .client_fetch_time = base::Time::Now(),
    });
  }
  latest_serial_number_ = seed.parsed.serial_number();
}

void VariationsSeedStore::StoreValidatedSafeSeed(
    const ValidatedSeed& seed,
    int seed_milestone,
    const ClientFilterableState& client_state,
    base::Time seed_fetch_time) {
  const StoredSeed previous_safe_seed = safe_seed_store_->GetCompressedSeed();
  // Before updating the safe seed, update the latest seed if the latest
  // seed's value is |kIdenticalToSafeSeedSentinel|.
  //
  // It's theoretically possible for the client to be in the following state:
  // 1. The client has safe seed A.
  // 2. The client is applying seed B. In other words, seed B was the latest
  //    seed when Chrome was started.
  // 3. The client has just successfully fetched a new latest seed that
  //    happens to be seed A—perhaps due to a rollback. In this case,
  //    |kIdenticalToSafeSeedSentinel| is stored as the latest seed value to
  //    avoid duplicating seed A in storage.
  // 4. The client is promoting seed B to safe seed.
  const StoredSeed latest_seed = seed_reader_writer_->GetSeedData();
  if (!seed.MatchesStoredSeed(previous_safe_seed) &&
      latest_seed.data == kIdenticalToSafeSeedSentinel) {
    // For the below call to StoreValidatedSeed(), there are two possibilities
    // to consider:
    //
    // 1. The client is in the SeedFile experiment's treatment group. In this
    //    case, StoreValidatedSeedInfo() updates the seed file and ignores the
    //    local state seed.
    // 2. The client is either not in the experiment or is in its control or
    //    default group. In this case, |previous_safe_seed.data| is ignored.
    seed_reader_writer_->StoreValidatedSeedInfo(ValidatedSeedInfo{
        .compressed_seed_data = previous_safe_seed.data,
        .base64_seed_data =
            local_state_->GetString(prefs::kVariationsSafeCompressedSeed),
        .signature = latest_seed.signature,
        .milestone = latest_seed.milestone,
        .seed_date = latest_seed.seed_date,
        .client_fetch_time = latest_seed.client_fetch_time,
    });
  }

  safe_seed_store_->SetCompressedSeed(ValidatedSeedInfo{
      .compressed_seed_data = seed.compressed_seed_data,
      .base64_seed_data = seed.base64_seed_data,
      .signature = seed.base64_seed_signature,
      .milestone = seed_milestone,
      .seed_date = client_state.reference_date,
      .client_fetch_time = seed_fetch_time,
  });

  safe_seed_store_->SetLocale(client_state.locale);
  safe_seed_store_->SetPermanentConsistencyCountry(
      client_state.permanent_consistency_country);
  safe_seed_store_->SetSessionConsistencyCountry(
      client_state.session_consistency_country);

  // As a space optimization, overwrite the stored latest seed data with an
  // alias to the safe seed, if they are identical.
  if (seed.MatchesStoredSeed(seed_reader_writer_->GetSeedData())) {
    seed_reader_writer_->StoreValidatedSeedInfo(ValidatedSeedInfo{
        .compressed_seed_data = kIdenticalToSafeSeedSentinel,
        .base64_seed_data = kIdenticalToSafeSeedSentinel,
        .signature = latest_seed.signature,
        .milestone = latest_seed.milestone,
        .seed_date = latest_seed.seed_date,
        .client_fetch_time = latest_seed.client_fetch_time,
    });

    // Moreover, in this case, the last fetch time for the safe seed should
    // match the latest seed's.
    safe_seed_store_->SetFetchTime(latest_seed.client_fetch_time);
  }

#if BUILDFLAG(IS_CHROMEOS)
  // `SendSafeSeedToPlatform` will send the safe seed at most twice and should
  // only be called if the seed is successfully validated.
  // This is a best effort attempt and it is possible that the safe seed for
  // platform and Chrome are different if sending the safe seed fails twice.
  send_seed_to_platform_attempts_ = 0;
  SendSafeSeedToPlatform(GetSafeSeedStateForPlatform(
      seed, seed_milestone, client_state, seed_fetch_time));
#endif  // BUILDFLAG(IS_CHROMEOS)
}

// static
VariationsSeedStore::SeedProcessingResult VariationsSeedStore::ProcessSeedData(
    bool signature_verification_enabled,
    SeedData seed_data) {
  const std::string* data = &seed_data.data;

  // If the data is gzip compressed, first uncompress it.
  std::string ungzipped_data;
  if (seed_data.is_gzip_compressed) {
    StoreSeedResult result = Uncompress(*data, &ungzipped_data);
    if (result != StoreSeedResult::kSuccess)
      return {std::move(seed_data), result};
    data = &ungzipped_data;
  }

  // If the data is delta-compressed, apply the delta patch.
  std::string patched_data;
  if (seed_data.is_delta_compressed) {
    DCHECK(!seed_data.existing_seed_bytes.empty());
    if (!ApplyDeltaPatch(seed_data.existing_seed_bytes, *data, &patched_data))
      return {std::move(seed_data), StoreSeedResult::kFailedDeltaApply};
    data = &patched_data;
  }

  ValidatedSeed validated;
  auto validate_result = VariationsSeedStore::ValidateSeedBytes(
      *data, seed_data.base64_seed_signature,
      VariationsSeedStore::SeedType::LATEST, signature_verification_enabled,
      &validated);
  // Important, this must come after the above call as `data` can point to a
  // member of `seed_data` which is being moved.
  SeedProcessingResult result(std::move(seed_data), StoreSeedResult::kSuccess);
  result.validate_result = validate_result;
  result.validated = std::move(validated);
  return result;
}

// static
StoreSeedResult VariationsSeedStore::ValidateSeedBytes(
    const std::string& seed_bytes,
    const std::string& base64_seed_signature,
    SeedType seed_type,
    bool signature_verification_enabled,
    ValidatedSeed* result) {
  DCHECK(result);
  if (seed_bytes.empty())
    return StoreSeedResult::kFailedEmptyGzipContents;

  // Only store the seed data if it parses correctly.
  VariationsSeed seed;
  if (!seed.ParseFromString(seed_bytes))
    return StoreSeedResult::kFailedParse;

  // TODO(crbug.com/40228403): get rid of |signature_verification_enabled| and
  // only support switches::kAcceptEmptySeedSignatureForTesting.
  if (signature_verification_enabled &&
      !AcceptEmptySeedSignatureForTesting(base64_seed_signature)) {
    const VerifySignatureResult verify_result =
        VerifySeedSignature(seed_bytes, base64_seed_signature);
    switch (seed_type) {
      case SeedType::LATEST:
        UMA_HISTOGRAM_ENUMERATION("Variations.StoreSeedSignature",
                                  verify_result,
                                  VerifySignatureResult::ENUM_SIZE);
        break;
      case SeedType::SAFE:
        UMA_HISTOGRAM_ENUMERATION(
            "Variations.SafeMode.StoreSafeSeed.SignatureValidity",
            verify_result, VerifySignatureResult::ENUM_SIZE);
        break;
    }

    if (verify_result != VerifySignatureResult::VALID_SIGNATURE)
      return StoreSeedResult::kFailedSignature;
  }

  std::string compressed_seed_data;
  if (!compression::GzipCompress(seed_bytes, &compressed_seed_data)) {
    return StoreSeedResult::kFailedGzip;
  }
  result->base64_seed_data = base::Base64Encode(compressed_seed_data);
  result->compressed_seed_data = std::move(compressed_seed_data);
  result->base64_seed_signature = base64_seed_signature;
  result->parsed.Swap(&seed);
  return StoreSeedResult::kSuccess;
}

// static
bool VariationsSeedStore::ApplyDeltaPatch(const std::string& existing_data,
                                          const std::string& patch,
                                          std::string* output) {
  output->clear();

  google::protobuf::io::CodedInputStream in(
      reinterpret_cast<const uint8_t*>(patch.data()), patch.length());
  // Temporary string declared outside the loop so it can be re-used between
  // different iterations (rather than allocating new ones).
  std::string temp;

  const uint32_t existing_data_size =
      static_cast<uint32_t>(existing_data.size());
  while (in.CurrentPosition() != static_cast<int>(patch.length())) {
    uint32_t value;
    if (!in.ReadVarint32(&value))
      return false;

    if (value != 0) {
      // A non-zero value indicates the number of bytes to copy from the patch
      // stream to the output.

      // No need to guard against bad data (i.e. very large |value|) because the
      // call below will fail if |value| is greater than the size of the patch.
      if (!in.ReadString(&temp, value))
        return false;
      output->append(temp);
    } else {
      // Otherwise, when it's zero, it indicates that it's followed by a pair of
      // numbers - |offset| and |length| that specify a range of data to copy
      // from |existing_data|.
      uint32_t offset;
      uint32_t length;
      if (!in.ReadVarint32(&offset) || !in.ReadVarint32(&length))
        return false;

      // Check for |offset + length| being out of range and for overflow.
      base::CheckedNumeric<uint32_t> end_offset(offset);
      end_offset += length;
      if (!end_offset.IsValid() || end_offset.ValueOrDie() > existing_data_size)
        return false;
      output->append(existing_data, offset, length);
    }
  }
  return true;
}

#if BUILDFLAG(IS_CHROMEOS)
featured::SeedDetails VariationsSeedStore::GetSafeSeedStateForPlatform(
    const ValidatedSeed& seed,
    const int seed_milestone,
    const ClientFilterableState& client_state,
    const base::Time seed_fetch_time) {
  featured::SeedDetails safe_seed;
  safe_seed.set_b64_compressed_data(seed.base64_seed_data);
  safe_seed.set_locale(client_state.locale);
  safe_seed.set_milestone(seed_milestone);
  safe_seed.set_permanent_consistency_country(
      client_state.permanent_consistency_country);
  safe_seed.set_session_consistency_country(
      client_state.session_consistency_country);
  safe_seed.set_signature(seed.base64_seed_signature);
  safe_seed.set_date(
      client_state.reference_date.ToDeltaSinceWindowsEpoch().InMilliseconds());
  safe_seed.set_fetch_time(
      seed_fetch_time.ToDeltaSinceWindowsEpoch().InMilliseconds());

  return safe_seed;
}

void VariationsSeedStore::MaybeRetrySendSafeSeed(
    const featured::SeedDetails& safe_seed,
    bool success) {
  // Do not retry after two failed attempts.
  if (!success &&
      send_seed_to_platform_attempts_ < kSendPlatformSafeSeedMaxAttempts) {
    SendSafeSeedToPlatform(safe_seed);
  }
}

void VariationsSeedStore::SendSafeSeedToPlatform(
    const featured::SeedDetails& safe_seed) {
  send_seed_to_platform_attempts_++;
  ash::featured::FeaturedClient* client = ash::featured::FeaturedClient::Get();
  if (client) {
    client->HandleSeedFetched(
        safe_seed, base::BindOnce(&VariationsSeedStore::MaybeRetrySendSafeSeed,
                                  weak_ptr_factory_.GetWeakPtr(), safe_seed));
  }
}
#endif  // BUILDFLAG(IS_CHROMEOS)

}  // namespace variations
