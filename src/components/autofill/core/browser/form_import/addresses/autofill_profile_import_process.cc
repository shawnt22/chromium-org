// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/core/browser/form_import/addresses/autofill_profile_import_process.h"

#include <algorithm>
#include <map>

#include "base/check_deref.h"
#include "base/feature_list.h"
#include "base/strings/utf_string_conversions.h"
#include "components/autofill/core/browser/data_manager/addresses/address_data_cleaner.h"
#include "components/autofill/core/browser/data_manager/addresses/address_data_manager.h"
#include "components/autofill/core/browser/data_model/addresses/autofill_profile.h"
#include "components/autofill/core/browser/data_model/addresses/autofill_profile_comparator.h"
#include "components/autofill/core/browser/data_quality/addresses/profile_requirement_utils.h"
#include "components/autofill/core/browser/field_types.h"
#include "components/autofill/core/browser/metrics/profile_import_metrics.h"
#include "components/autofill/core/common/autofill_clock.h"
#include "components/autofill/core/common/autofill_features.h"

namespace autofill {

namespace {

using UserDecision = AutofillClient::AddressPromptUserDecision;

// When the profile is observed without explicit country information, Autofill
// guesses it's country. Detecting a profile as a duplicate can fail if we guess
// incorrectly. This function checks if we have reason to believe that the
// country of `profile` was guessed incorrectly. It does so by checking whether
// any of the `existing_profiles` becomes mergeable after removing the country
// of `profile`.
// Comparisons are done using `comparator`. Note that for two countries to be
// mergeable, they must share the same address model.
bool ShouldCountryApproximationBeRemoved(
    const AutofillProfile& profile,
    const std::vector<const AutofillProfile*>& existing_profiles,
    const AutofillProfileComparator& comparator) {
  auto IsMergeableWithExistingProfiles = [&](const AutofillProfile& profile) {
    return std::ranges::any_of(existing_profiles, [&](auto* existing_profile) {
      return comparator.AreMergeable(profile, *existing_profile);
    });
  };
  if (IsMergeableWithExistingProfiles(profile)) {
    return false;
  }
  AutofillProfile without_country = profile;
  without_country.ClearFields({ADDRESS_HOME_COUNTRY});
  return IsMergeableWithExistingProfiles(without_country);
}

}  // namespace

ProfileImportMetadata::ProfileImportMetadata() = default;
ProfileImportMetadata::ProfileImportMetadata(const ProfileImportMetadata&) =
    default;
ProfileImportMetadata::~ProfileImportMetadata() = default;

ProfileImportProcess::ProfileImportProcess(
    const AutofillProfile& observed_profile,
    const std::string& app_locale,
    const GURL& form_source_url,
    ukm::SourceId ukm_source_id,
    AddressDataManager* address_data_manager,
    bool allow_only_silent_updates,
    ProfileImportMetadata import_metadata)
    : observed_profile_(observed_profile),
      app_locale_(app_locale),
      form_source_url_(form_source_url),
      ukm_source_id_(ukm_source_id),
      address_data_manager_(CHECK_DEREF(address_data_manager)),
      allow_only_silent_updates_(allow_only_silent_updates),
      import_metadata_(import_metadata) {
  DetermineProfileImportType();
  DetermineSourceOfImportCandidate();
}

ProfileImportProcess::ProfileImportProcess(const ProfileImportProcess&) =
    default;

ProfileImportProcess& ProfileImportProcess::operator=(
    const ProfileImportProcess& other) = default;

ProfileImportProcess::~ProfileImportProcess() = default;

bool ProfileImportProcess::prompt_shown() const {
  return prompt_shown_;
}

bool ProfileImportProcess::UserDeclined() const {
  return user_decision_ == UserDecision::kDeclined ||
         user_decision_ == UserDecision::kEditDeclined ||
         user_decision_ == UserDecision::kMessageDeclined ||
         user_decision_ == UserDecision::kNever;
}

bool ProfileImportProcess::UserAccepted() const {
  return user_decision_ == UserDecision::kAccepted ||
         user_decision_ == UserDecision::kEditAccepted;
}

void ProfileImportProcess::DetermineProfileImportType() {
  AutofillProfileComparator comparator(app_locale_);
  bool is_mergeable_with_existing_profile = false;
  int number_of_unchanged_profiles = 0;
  int number_of_blocked_profile_updates = 0;
  std::optional<AutofillProfile> migration_candidate;

  // We don't offer an import if `observed_profile_` is a duplicate of an
  // existing profile.
  const std::vector<const AutofillProfile*> existing_profiles =
      address_data_manager_->GetProfiles(
          AddressDataManager::ProfileOrder::kMostRecentlyUsedFirstDesc);

  // If we have reason to believe that the country was complemented incorrectly,
  // remove it.
  if (import_metadata_.did_complement_country &&
      ShouldCountryApproximationBeRemoved(observed_profile_, existing_profiles,
                                          comparator)) {
    observed_profile_.ClearFields({ADDRESS_HOME_COUNTRY});
    import_metadata_.did_complement_country = false;
  }

  for (const AutofillProfile* existing_profile : existing_profiles) {
    // If the existing profile is not mergeable with the observed profile, the
    // existing profile is not altered by this import.
    if (!comparator.AreMergeable(*existing_profile, observed_profile_)) {
      ++number_of_unchanged_profiles;
      continue;
    }

    // The observed profile is mergeable with an existing profile.
    // This information is used to determine if the observed profile classifies
    // as an import of a new profile or the import of a duplicate profile.
    is_mergeable_with_existing_profile = true;

    // Make a copy of the existing profile and merge it with the observation.
    // The return value of |MergeDataFrom()| indicates if the existing profile
    // was changed at all during that merge.
    AutofillProfile merged_profile = *existing_profile;
    if (!merged_profile.MergeDataFrom(observed_profile_, app_locale_)) {
      ++number_of_unchanged_profiles;
      // The `observed_profile_` is a duplicate of the `existing_profile`.
      // Consider it for migration.
      MaybeSetMigrationCandidate(migration_candidate, *existing_profile);
      continue;
    }

    // At this point, the observed profile was merged with (a copy of) the
    // existing profile which changed in some way.
    // Now, determine if the merge alters any settings-visible value, or if the
    // merge can be considered as a silent update that does not need to get user
    // confirmation.
    if (AutofillProfileComparator::ProfilesHaveDifferentSettingsVisibleValues(
            *existing_profile, merged_profile, app_locale_)) {
      if (allow_only_silent_updates_) {
        ++number_of_unchanged_profiles;
        continue;
      }

      // Determine if the existing profile is blocked for updates.
      // If the address data manager is not available the profile is considered
      // as not blocked. Also, updates can be disabled by a feature flag.
      bool is_blocked_for_update =
          address_data_manager_->IsProfileUpdateBlocked(
              existing_profile->guid()) ||
          base::FeatureList::IsEnabled(
              features::test::kAutofillDisableProfileUpdates);

      if (is_blocked_for_update) {
        ++number_of_blocked_profile_updates;
      }

      // If a settings-visible value changed, the existing profile is the merge
      // candidate if no other merge candidate has already been found and if the
      // existing profile is not blocked for updates.
      if (!merge_candidate_.has_value() && !is_blocked_for_update) {
        merge_candidate_ = *existing_profile;
        import_candidate_ = merged_profile;
      } else {
        // If there is already a merge candidate, the existing profile is not
        // supposed to be changed.
        ++number_of_unchanged_profiles;
      }
      continue;
    }
    // If the profile changed but all settings-visible values are maintained,
    // the profile can be updated silently. Silent updates can also be disabled
    // using a feature flag.
    if (!base::FeatureList::IsEnabled(
            features::test::kAutofillDisableSilentProfileUpdates)) {
      merged_profile.usage_history().set_modification_date(
          AutofillClock::Now());
      silently_updated_profiles_.emplace_back(merged_profile);
    } else {
      ++number_of_unchanged_profiles;
    }
    // The `observed_profile_` only differs from the `existing_profile` in a
    // non-settings visible way. Consider it for migration.
    MaybeSetMigrationCandidate(migration_candidate, merged_profile);
  }

  // If the profile is not mergeable with an existing profile, the import
  // corresponds to a new profile.
  if (!is_mergeable_with_existing_profile) {
    if (!allow_only_silent_updates_) {
      // There should be no import candidate yet.
      DCHECK(!import_candidate_.has_value());
      if (address_data_manager_->IsNewProfileImportBlockedForDomain(
              form_source_url_)) {
        import_type_ = AutofillProfileImportType::kSuppressedNewProfile;
      } else {
        import_type_ = AutofillProfileImportType::kNewProfile;
        import_candidate_ = observed_profile();
      }
    } else {
      import_type_ = AutofillProfileImportType::kUnusableIncompleteProfile;
    }
  } else {
    bool silent_updates_present = !silently_updated_profiles_.empty();

    if (merge_candidate_.has_value()) {
      import_type_ =
          silent_updates_present
              ? AutofillProfileImportType::kConfirmableMergeAndSilentUpdate
              : AutofillProfileImportType::kConfirmableMerge;
    } else if (number_of_blocked_profile_updates > 0) {
      import_type_ =
          silent_updates_present
              ? AutofillProfileImportType::
                    kSuppressedConfirmableMergeAndSilentUpdate
              : AutofillProfileImportType::kSuppressedConfirmableMerge;
    } else if (allow_only_silent_updates_) {
      import_type_ =
          silent_updates_present
              ? AutofillProfileImportType::kSilentUpdateForIncompleteProfile
              : AutofillProfileImportType::kUnusableIncompleteProfile;
    } else if (!migration_candidate) {
      import_type_ = silent_updates_present
                         ? AutofillProfileImportType::kSilentUpdate
                         : AutofillProfileImportType::kDuplicateImport;
    } else {
      import_type_ =
          silent_updates_present
              ? AutofillProfileImportType::kProfileMigrationAndSilentUpdate
              : AutofillProfileImportType::kProfileMigration;
      CHECK(migration_candidate.has_value());
      import_candidate_ = std::move(migration_candidate);
    }
  }

  if (import_candidate_.has_value()) {
    import_candidate_->usage_history().set_modification_date(
        AutofillClock::Now());
  }

  // At this point, all existing profiles are either unchanged, updated and/or
  // one is the merge candidate.
  // One of the unchanged or updated profiles might be considered for migration.
  // In this case, `import_type()` is `kProfileMigrationAndMaybeSilentUpdates`.
  DCHECK_EQ(existing_profiles.size(),
            number_of_unchanged_profiles + silently_updated_profiles_.size() +
                (merge_candidate_.has_value() ? 1 : 0));
  DCHECK_NE(import_type_, AutofillProfileImportType::kImportTypeUnspecified);
}

void ProfileImportProcess::DetermineSourceOfImportCandidate() {
  // Even though kHomeAndWorkSuperset is a kind of new profile, it doesn't need
  // to be handled here, since H/W is only available for users eligible to
  // account address storage.
  if (import_type_ != AutofillProfileImportType::kNewProfile) {
    return;
  }
  CHECK(import_candidate_);
  if (address_data_manager_->IsEligibleForAddressAccountStorage()) {
    import_candidate_ = import_candidate_->ConvertToAccountProfile();
  }
}

void ProfileImportProcess::MaybeSetMigrationCandidate(
    std::optional<AutofillProfile>& migration_candidate,
    const AutofillProfile& profile) const {
  // Basic checks: No migration candidate was selected yet, prompts can be shown
  // (i.e. not only silent updates) and the `profile` is not stored in the
  // user's account already.
  if (migration_candidate || allow_only_silent_updates_ ||
      profile.IsAccountProfile()) {
    return;
  }
  // Check the eligiblity of the user and profile.
  if (IsEligibleForMigrationToAccount(*address_data_manager_, profile)) {
    migration_candidate = profile;
  }
}

void ProfileImportProcess::ApplyImport() {
  // At this point, a user decision must have been supplied.
  DCHECK_NE(user_decision_, UserDecision::kUndefined);
  if (!ProfilesChanged()) {
    return;
  }

  // Apply silent updates.
  for (const AutofillProfile& updated_profile : silently_updated_profiles_) {
    address_data_manager_->UpdateProfile(updated_profile);
  }

  if (!confirmed_import_candidate_.has_value()) {
    return;
  }
  const AutofillProfile& confirmed_profile = *confirmed_import_candidate_;
  // Confirming an import candidate corresponds to either a new/update profile
  // or a migration prompt.
  if (is_migration()) {
    address_data_manager_->MigrateProfileToAccount(confirmed_profile);
  } else if (is_confirmable_update()) {
    address_data_manager_->UpdateProfile(confirmed_profile);
  } else {
    address_data_manager_->AddProfile(confirmed_profile);
  }
}

void ProfileImportProcess::SetUserDecision(
    UserDecision decision,
    base::optional_ref<const AutofillProfile> edited_profile) {
  // A user decision should only be supplied once.
  DCHECK_EQ(user_decision_, UserDecision::kUndefined);
  DCHECK(!confirmed_import_candidate_.has_value());

  user_decision_ = decision;
  switch (user_decision_) {
    // If the import was accepted either with or without a prompt, the import
    // candidate gets confirmed.
    case UserDecision::kUserNotAsked:
    case UserDecision::kAccepted:
      confirmed_import_candidate_ = import_candidate_;
      break;

    case UserDecision::kEditAccepted:
      // If the import candidate is supplied, the 'edited_profile' must be
      // supplied.
      DCHECK(edited_profile.has_value());
      confirmed_import_candidate_ = edited_profile.value();

      // Make sure the verification status of all settings-visible non-empty
      // fields in the edited profile are set to kUserVerified.
      for (auto type : edited_profile->GetUserVisibleTypes()) {
        std::u16string value = confirmed_import_candidate_->GetRawInfo(type);
        if (!value.empty() &&
            confirmed_import_candidate_->GetVerificationStatus(type) ==
                VerificationStatus::kNoStatus) {
          confirmed_import_candidate_->SetRawInfoWithVerificationStatus(
              type, value, VerificationStatus::kUserVerified);
        }
      }

      confirmed_import_candidate_->FinalizeAfterImport();
      confirmed_import_candidate_->usage_history().set_modification_date(
          AutofillClock::Now());
      // The `confirmed_import_candidate_` has to have the same `guid` as the
      // original import candidate.
      DCHECK_EQ(import_candidate_->guid(), confirmed_import_candidate_->guid());
      break;

    // If the confirmable merge was declided or ignored, the original merge
    // candidate should be maintined. Note that the decline/ignore/never does
    // not mean that silent updates are not performed.
    case UserDecision::kDeclined:
    case UserDecision::kEditDeclined:
    case UserDecision::kMessageDeclined:
    case UserDecision::kMessageTimeout:
    case UserDecision::kIgnored:
    case UserDecision::kAutoDeclined:
    case UserDecision::kNever:
      confirmed_import_candidate_ = merge_candidate_;
      break;

    case UserDecision::kUndefined:
      NOTREACHED();
  }
}

void ProfileImportProcess::AcceptWithoutPrompt() {
  SetUserDecision(UserDecision::kUserNotAsked);
}

void ProfileImportProcess::AcceptWithoutEdits() {
  SetUserDecision(UserDecision::kAccepted);
}

void ProfileImportProcess::AcceptWithEdits(AutofillProfile edited_profile) {
  SetUserDecision(UserDecision::kEditAccepted,
                  std::make_optional(edited_profile));
}

void ProfileImportProcess::Declined() {
  SetUserDecision(UserDecision::kDeclined);
}

void ProfileImportProcess::Ignore() {
  SetUserDecision(UserDecision::kIgnored);
}

bool ProfileImportProcess::ProfilesChanged() const {
  // At this point, a user decision must have been supplied.
  DCHECK_NE(user_decision_, UserDecision::kUndefined);

  if (!silently_updated_profiles_.empty()) {
    return true;
  }

  // If there is no confirmed import candidate there are no changes.
  if (!confirmed_import_candidate_.has_value()) {
    return false;
  }

  // If the import was accepted, return true.
  if (user_decision_ == UserDecision::kAccepted ||
      user_decision_ == UserDecision::kEditAccepted ||
      user_decision_ == UserDecision::kUserNotAsked) {
    return true;
  }

  return false;
}

void ProfileImportProcess::set_prompt_was_shown() {
  prompt_shown_ = true;
}

void ProfileImportProcess::CollectMetrics(
    ukm::UkmRecorder* ukm_recorder,
    const std::vector<const AutofillProfile*>& existing_profiles) const {
  // Metrics should only be recorded after a user decision was supplied.
  DCHECK_NE(user_decision_, UserDecision::kUndefined);

  auto LogUkmMetrics = [&](int num_edited_fields = 0) {
    autofill_metrics::LogAddressProfileImportUkm(
        ukm_recorder, ukm_source_id_, import_type_, user_decision_,
        import_metadata_, num_edited_fields,
        UserAccepted() ? confirmed_import_candidate_ : import_candidate_,
        existing_profiles, app_locale_);
  };

  if (allow_only_silent_updates_) {
    // Record the import type for the silent updates.
    autofill_metrics::LogSilentUpdatesProfileImportType(import_type_);
    if (import_type_ == AutofillProfileImportType::kSilentUpdate ||
        import_type_ ==
            AutofillProfileImportType::kSilentUpdateForIncompleteProfile) {
      LogUkmMetrics();
    }
    return;
  }

  // For any finished import process record the type of the import.
  autofill_metrics::LogProfileImportType(import_type_);

  // Tracks number of edited fields by the user in the storage prompt. Zero if
  // the user didn't edit any fields.
  int num_edited_fields = CollectedEditedTypeHistograms();

  // For an import process that involves prompting the user, record the
  // decision.
  if (import_type_ == AutofillProfileImportType::kNewProfile) {
    autofill_metrics::LogNewProfileImportDecision(
        user_decision_, existing_profiles,
        UserAccepted() ? *confirmed_import_candidate_ : *import_candidate_,
        app_locale_);
    LogUkmMetrics(num_edited_fields);
    if (UserAccepted()) {
      autofill_metrics::LogNewProfileStorageLocation(
          *confirmed_import_candidate_);
    }
  } else if (import_type_ == AutofillProfileImportType::kHomeAndWorkSuperset) {
    autofill_metrics::LogHomeWorkSupersetImportDecision(user_decision_);
  } else if (is_confirmable_update()) {
    autofill_metrics::LogProfileUpdateImportDecision(
        user_decision_, existing_profiles,
        UserAccepted() ? *confirmed_import_candidate_ : *import_candidate_,
        app_locale_);

    DCHECK(merge_candidate_.has_value() && import_candidate_.has_value());
    // For all update prompts, log the field types and total number of fields
    // that would change due to the update. Note that this does not include
    // additional manual edits the user can perform in the storage dialog.
    // Those are covered separately below.
    const std::vector<ProfileValueDifference> merge_difference =
        AutofillProfileComparator::GetSettingsVisibleProfileDifference(
            import_candidate_.value(), merge_candidate_.value(), app_locale_);

    for (const auto& difference : merge_difference) {
      autofill_metrics::LogProfileUpdateAffectedType(difference.type,
                                                     user_decision_);
    }
    autofill_metrics::LogUpdateProfileNumberOfAffectedFields(
        merge_difference.size(), user_decision_);
    LogUkmMetrics(num_edited_fields);
  } else if (import_type_ == AutofillProfileImportType::kSilentUpdate) {
    LogUkmMetrics();
  } else if (is_migration()) {
    autofill_metrics::LogProfileMigrationImportDecision(user_decision_);
    LogUkmMetrics(num_edited_fields);
  }
}

int ProfileImportProcess::CollectedEditedTypeHistograms() const {
  if (user_decision_ != UserDecision::kEditAccepted) {
    return 0;
  }
  // Compute the number of edited settings-visible fields.
  std::vector<ProfileValueDifference> edit_difference =
      AutofillProfileComparator::GetSettingsVisibleProfileDifference(
          *import_candidate_, *confirmed_import_candidate_, app_locale_);
  // Log edited types.
  for (const ProfileValueDifference& difference : edit_difference) {
    if (import_type_ == AutofillProfileImportType::kNewProfile) {
      autofill_metrics::LogNewProfileEditedType(difference.type);
    } else if (is_confirmable_update()) {
      autofill_metrics::LogProfileUpdateEditedType(difference.type);
    } else {
      CHECK(is_migration());
      autofill_metrics::LogProfileMigrationEditedType(difference.type);
    }
  }
  return edit_difference.size();
}

}  // namespace autofill
