// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "enterprise_search_aggregator_provider.h"

#include <algorithm>
#include <ctime>
#include <functional>
#include <iomanip>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/containers/contains.h"
#include "base/containers/fixed_flat_map.h"
#include "base/i18n/case_conversion.h"
#include "base/json/json_reader.h"
#include "base/metrics/histogram_functions.h"
#include "base/notreached.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/types/expected.h"
#include "base/values.h"
#include "components/omnibox/browser/autocomplete_enums.h"
#include "components/omnibox/browser/autocomplete_input.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/omnibox/browser/autocomplete_match_classification.h"
#include "components/omnibox/browser/autocomplete_provider.h"
#include "components/omnibox/browser/autocomplete_provider_client.h"
#include "components/omnibox/browser/autocomplete_provider_listener.h"
#include "components/omnibox/browser/remote_suggestions_service.h"
#include "components/omnibox/browser/search_suggestion_parser.h"
#include "components/omnibox/common/omnibox_feature_configs.h"
#include "components/omnibox/common/string_cleaning.h"
#include "components/search/search.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_data.h"
#include "components/search_engines/template_url_service.h"
#include "components/strings/grit/components_strings.h"
#include "components/url_formatter/url_formatter.h"
#include "services/data_decoder/public/cpp/data_decoder.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "third_party/re2/src/re2/re2.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"

namespace {

// Whether to make multiple requests to the backend.
bool kMultipleRequests() {
  return omnibox_feature_configs::SearchAggregatorProvider::Get()
      .multiple_requests;
}

// Limit the number matches created for each type, not total, as a performance
// guard.
size_t kMaxMatchesCreatedPerType() {
  return omnibox_feature_configs::SearchAggregatorProvider::Get()
      .scoring_max_matches_created_per_type;
}

// Limit the number of matches shown for each type, not total. Needed to prevent
// inputs like 'joe' or 'doc' from flooding the results with `PEOPLE` or
// `CONTENT` suggestions. More matches may be created in order to ensure the
// best matches are shown.
size_t kMaxScopedMatchesShownPerType() {
  return omnibox_feature_configs::SearchAggregatorProvider::Get()
      .scoring_max_scoped_matches_shown_per_type;
}
size_t kMaxUnscopedMatchesShownPerType() {
  return omnibox_feature_configs::SearchAggregatorProvider::Get()
      .scoring_max_unscoped_matches_shown_per_type;
}

// Score matches based on text similarity of the input and match fields.
// - Strong matches are input words at least 3 chars long that match the
//   suggestion content or description.
// - For PEOPLE suggestions, input words of 1 or 2 chars are strong matches if
//   they fully match (rather than prefix match) the suggestion content or
//   description. E.g. "jo" will be a strong match for "Jo Jacob", but "ja"
//   won't.
// - Weak matches are input words shorter than 3 chars or that match elsewhere
//   in the match fields.
// TODO(manukh): For consistency, rename "Text" to "Word" when finch params are
//   expired.
size_t kMinCharForStrongTextMatch() {
  return omnibox_feature_configs::SearchAggregatorProvider::Get()
      .scoring_min_char_for_strong_text_match;
}

// If a) every input word is a strong match, and b) there are at least 2 such
// matches, score matches 1000.
size_t kMinWordsForFullTextMatchBoost() {
  return omnibox_feature_configs::SearchAggregatorProvider::Get()
      .scoring_min_words_for_full_text_match_boost;
}
int kFullTextMatchScore() {
  return omnibox_feature_configs::SearchAggregatorProvider::Get()
      .scoring_full_text_match_score;
}

// Otherwise, score using a weighted sum of the # of strong and weak matches.
int kScorePerStrongTextMatch() {
  return omnibox_feature_configs::SearchAggregatorProvider::Get()
      .scoring_score_per_strong_text_match;
}
int kScorePerWeakTextMatch() {
  return omnibox_feature_configs::SearchAggregatorProvider::Get()
      .scoring_score_per_weak_text_match;
}
int kMaxTextScore() {
  return omnibox_feature_configs::SearchAggregatorProvider::Get()
      .scoring_max_text_score;
}

// Shift people relevances whose email username was exactly matched by an input
// term. Some people-seeking inputs will have words intended to match email
// usernames and scoring these 400 wouldn't reliably allow them to make it to
// the final results.
int kPeopleEmailMatchScoreBoost() {
  return omnibox_feature_configs::SearchAggregatorProvider::Get()
      .scoring_people_email_match_score_boost;
}

// Shift people relevances higher than calculated with the above constants. Most
// people-seeking inputs will have 2 words (firstname, lastname) and scoring
// these 800 wouldn't reliably allow them to make it to the final results.
int kPeopleScoreBoost() {
  return omnibox_feature_configs::SearchAggregatorProvider::Get()
      .scoring_people_score_boost;
}

// When suggestions equally match the input, prefer showing content over query
// suggestions. This wont affect ranking due to grouping, only which suggestions
// are shown. This won't affect people suggestions unless `kPeopleScoreBoost` is
// 0.
bool kPreferContentsOverQueries() {
  return omnibox_feature_configs::SearchAggregatorProvider::Get()
      .scoring_prefer_contents_over_queries;
}

// Always show at least 2 (unscoped) or 8 (scoped) suggestions if available.
// Only show more if they're scored at least 500; i.e. had at least 1 strong and
// 1 weak match.
size_t kScopedMaxLowQualityMatches() {
  return omnibox_feature_configs::SearchAggregatorProvider::Get()
      .scoring_scoped_max_low_quality_matches;
}
size_t kUnscopedMaxLowQualityMatches() {
  return omnibox_feature_configs::SearchAggregatorProvider::Get()
      .scoring_unscoped_max_low_quality_matches;
}
int kLowQualityThreshold() {
  // When this is converted back to a `constexpr`, it should be relative to
  // `scoring_score_per_strong_text_match` & `scoring_score_per_weak_text_match`
  // instead of an independent int.
  return omnibox_feature_configs::SearchAggregatorProvider::Get()
      .scoring_low_quality_threshold;
}

// Helper for reading possibly null paths from `base::Value::Dict`.
std::string ptr_to_string(const std::string* ptr) {
  return ptr ? *ptr : "";
}

// A mapping from `mime_type` to the human readable `file_type_description` for
// selected MIME types.
// Mappings documentation:
// https://developers.google.com/drive/api/guides/mime-types
// https://developers.google.com/drive/api/guides/ref-export-formats
const auto kMimeTypeMapping = base::MakeFixedFlatMap<std::string_view, int>({
    {"application/json", IDS_CONTENT_SUGGESTION_DESCRIPTION_JSON},
    {"application/rtf", IDS_CONTENT_SUGGESTION_DESCRIPTION_RICH_TEXT_FORMAT},
    {"application/pdf", IDS_CONTENT_SUGGESTION_DESCRIPTION_PDF},
    {"application/vnd.google-apps.document",
     IDS_CONTENT_SUGGESTION_DESCRIPTION_GOOGLE_DOCS},
    {"application/vnd.google-apps.drawing",
     IDS_CONTENT_SUGGESTION_DESCRIPTION_GOOGLE_DRAWINGS},
    {"application/vnd.google-apps.folder",
     IDS_CONTENT_SUGGESTION_DESCRIPTION_GOOGLE_DRIVE_FOLDER},
    {"application/vnd.google-apps.form",
     IDS_CONTENT_SUGGESTION_DESCRIPTION_GOOGLE_FORMS},
    {"application/vnd.google-apps.jam",
     IDS_CONTENT_SUGGESTION_DESCRIPTION_GOOGLE_JAMBOARD},
    {"application/vnd.google-apps.photo",
     IDS_CONTENT_SUGGESTION_DESCRIPTION_GOOGLE_PHOTOS},
    {"application/vnd.google-apps.presentation",
     IDS_CONTENT_SUGGESTION_DESCRIPTION_GOOGLE_SLIDES},
    {"application/vnd.google-apps.script",
     IDS_CONTENT_SUGGESTION_DESCRIPTION_GOOGLE_APPS_SCRIPT},
    {"application/vnd.google-apps.site",
     IDS_CONTENT_SUGGESTION_DESCRIPTION_GOOGLE_SITES},
    {"application/vnd.google-apps.spreadsheet",
     IDS_CONTENT_SUGGESTION_DESCRIPTION_GOOGLE_SHEETS},
    {"application/"
     "vnd.openxmlformats-officedocument.presentationml.presentation",
     IDS_CONTENT_SUGGESTION_DESCRIPTION_MS_POWERPOINT},
    {"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",
     IDS_CONTENT_SUGGESTION_DESCRIPTION_MS_EXCEL},
    {"application/vnd.openxmlformats-officedocument.wordprocessingml.document",
     IDS_CONTENT_SUGGESTION_DESCRIPTION_MS_WORD},
    {"application/vnd.oasis.opendocument.presentation",
     IDS_CONTENT_SUGGESTION_DESCRIPTION_OPEN_DOCUMENT_PRESENTATION},
    {"application/vnd.oasis.opendocument.spreadsheet",
     IDS_CONTENT_SUGGESTION_DESCRIPTION_OPEN_DOCUMENT_SPREADSHEET},
    {"application/vnd.oasis.opendocument.text",
     IDS_CONTENT_SUGGESTION_DESCRIPTION_OPEN_DOCUMENT_TEXT},
    {"application/zip", IDS_CONTENT_SUGGESTION_DESCRIPTION_ZIP_FILE},
    {"image/jpeg", IDS_CONTENT_SUGGESTION_DESCRIPTION_IMAGE_JPEG},
    {"image/png", IDS_CONTENT_SUGGESTION_DESCRIPTION_IMAGE_PNG},
    {"image/svg+xml", IDS_CONTENT_SUGGESTION_DESCRIPTION_IMAGE_SVG},
    {"text/csv", IDS_CONTENT_SUGGESTION_DESCRIPTION_COMMA_SEPARATED_VALUES},
    {"text/markdown", IDS_CONTENT_SUGGESTION_DESCRIPTION_MARKDOWN},
    {"text/plain", IDS_CONTENT_SUGGESTION_DESCRIPTION_PLAIN_TEXT},
    {"video/mp4", IDS_CONTENT_SUGGESTION_DESCRIPTION_VIDEO_MP4},
    {"video/quicktime", IDS_CONTENT_SUGGESTION_DESCRIPTION_VIDEO_QUICKTIME},
    {"video/webm", IDS_CONTENT_SUGGESTION_DESCRIPTION_VIDEO_WEBM},
});

// A mapping from `source_type` to the human readable
// `content_type_description`.
const auto kSourceTypeMapping = base::MakeFixedFlatMap<std::string_view, int>({
    {"buganizer", IDS_CONTENT_SUGGESTION_DESCRIPTION_BUGANIZER},
    {"jira", IDS_CONTENT_SUGGESTION_DESCRIPTION_JIRA},
    {"salesforce", IDS_CONTENT_SUGGESTION_DESCRIPTION_SALESFORCE},
    {"slack", IDS_CONTENT_SUGGESTION_DESCRIPTION_SLACK},
});

// Helper for converting `mime_type` and `source_type` into a human readable
// string. Prioritizes `mime_type` over `source_type`.
std::u16string ContentTypeToDescription(const std::string_view& mime_type,
                                        const std::string_view& source_type) {
  const auto mimeTypeIter = kMimeTypeMapping.find(mime_type);
  if (mimeTypeIter != kMimeTypeMapping.end()) {
    return l10n_util::GetStringUTF16(mimeTypeIter->second);
  }
  const auto sourceTypeIter = kSourceTypeMapping.find(source_type);
  return sourceTypeIter != kSourceTypeMapping.end()
             ? l10n_util::GetStringUTF16(sourceTypeIter->second)
             : std::u16string();
}

// Helper for converting unix timestamp `time` into an abbreviated date.
// For time within the current day, return the time of day. (Ex. '12:45 PM')
// For time within the current year, return the abbreviated date. (Ex. 'Jan 02')
// Otherwise, return the full date. (Ex. '10/7/24')
const std::u16string UpdateTimeToString(std::optional<int> time) {
  if (!time) {
    return u"";
  }

  std::time_t unix_time = static_cast<std::time_t>(time.value());
  std::tm* local_time = std::localtime(&unix_time);
  if (!local_time) {
    return u"";
  }

  // Get current time to check if `unix_time` is in the current day or year.
  base::Time check_time = base::Time::FromTimeT(unix_time);
  base::Time now = base::Time::Now();

  return AutocompleteProvider::LocalizedLastModifiedString(now, check_time);
}

// Helper for getting the correct `TemplateURL` based on the input.
const TemplateURL* AdjustTemplateURL(AutocompleteInput* input,
                                     TemplateURLService* turl_service) {
  DCHECK(turl_service);
  return input->InKeywordMode()
             ? AutocompleteInput::GetSubstitutingTemplateURLForInput(
                   turl_service, input)
             : turl_service->GetEnterpriseSearchAggregatorEngine();
}

EnterpriseSearchAggregatorProvider::RelevanceData GetServerRelevanceData(
    const base::Value::Dict& result) {
  return {static_cast<int>(result.FindDouble("score").value_or(0) * 1000), 0, 0,
          "server"};
}

// Helpers to convert vector of strings to sets of words.
std::set<std::u16string> GetWords(std::vector<std::u16string> strings) {
  std::set<std::u16string> words = {};
  for (const auto& string : strings) {
    auto string_words = String16VectorFromString16(
        string_cleaning::CleanUpTitleForMatching(string), nullptr);
    std::move(string_words.begin(), string_words.end(),
              std::inserter(words, words.begin()));
  }
  return words;
}
std::set<std::u16string> GetWords(std::vector<std::string> strings) {
  std::vector<std::u16string> u16strings;
  std::ranges::transform(
      strings, std::back_inserter(u16strings),
      [](const auto& string) { return base::UTF8ToUTF16(string); });
  return GetWords(u16strings);
}

// Helper for getting a list of lowercase email usernames from the result
// dictionary.
const std::vector<std::u16string> GetEmailUsernames(
    const base::Value::Dict& result) {
  std::vector<std::u16string> usernames;
  const base::Value::List* emails =
      result.FindListByDottedPath("document.derivedStructData.emails");
  if (!emails) {
    return usernames;
  }
  for (const auto& email : *emails) {
    const std::string* email_value = email.GetDict().FindString("value");
    if (!email_value) {
      continue;
    }
    size_t at_pos = email_value->find('@');
    if (at_pos != std::string::npos) {
      usernames.push_back(base::i18n::ToLower(
          base::UTF8ToUTF16(email_value->substr(0, at_pos))));
    }
  }
  return usernames;
}

// Whether `word` matches any of `potential_match_words`.
enum class WordMatchType {
  NONE = 0,
  PREFIX,  // E.g. 'goo' prefixes 'goo' and 'google'.
  EXACT,   // E.g. 'goo' exactly matches 'goo' but not 'google'.
};
WordMatchType GetWordMatchType(std::u16string word,
                               std::set<std::u16string> potential_match_words) {
  auto it = potential_match_words.lower_bound(word);
  if (it == potential_match_words.end()) {
    return WordMatchType::NONE;
  }
  if (word == *it) {
    return WordMatchType::EXACT;
  }
  if (base::StartsWith(*it, word, base::CompareCase::SENSITIVE)) {
    return WordMatchType::PREFIX;
  }
  return WordMatchType::NONE;
}

// Returns 0 if the match should be filtered out.
EnterpriseSearchAggregatorProvider::RelevanceData CalculateRelevanceData(
    std::set<std::u16string> input_words,
    bool in_keyword_mode,
    AutocompleteMatch::EnterpriseSearchAggregatorType suggestion_type,
    const std::vector<std::string> strong_scoring_fields,
    const std::vector<std::string> weak_scoring_fields,
    const std::vector<std::u16string> email_usernames) {
  // Split match fields into words.
  std::set<std::u16string> strong_scoring_words =
      GetWords(strong_scoring_fields);
  std::set<std::u16string> weak_scoring_words = GetWords(weak_scoring_fields);
  // Do not use `GetWords()` for email usernames as it may split the username by
  // special symbols leading to false positives in "exact" matching.
  std::set<std::u16string> email_usernames_words;
  std::ranges::transform(
      email_usernames,
      std::inserter(email_usernames_words, email_usernames_words.end()),
      [](const std::u16string& email_username) { return email_username; });

  // Compute text similarity of the input and match fields. See comment for
  // `kMinCharForStrongTextMatch`.
  size_t strong_word_matches = 0;
  size_t weak_word_matches = 0;
  bool has_email_match = false;
  for (const auto& input_word : input_words) {
    WordMatchType strong_match_type =
        GetWordMatchType(input_word, strong_scoring_words);
    if (strong_match_type == WordMatchType::EXACT &&
        suggestion_type ==
            AutocompleteMatch::EnterpriseSearchAggregatorType::PEOPLE) {
      strong_word_matches++;
    } else if (strong_match_type != WordMatchType::NONE) {
      if (input_word.size() >= kMinCharForStrongTextMatch()) {
        strong_word_matches++;
      } else {
        weak_word_matches++;
      }
    } else if (GetWordMatchType(input_word, weak_scoring_words) !=
               WordMatchType::NONE) {
      weak_word_matches++;
    }
    // Check if the input has exact match with the email username fields for
    // people suggestions.
    if (!has_email_match &&
        suggestion_type ==
            AutocompleteMatch::EnterpriseSearchAggregatorType::PEOPLE &&
        GetWordMatchType(input_word, email_usernames_words) ==
            WordMatchType::EXACT) {
      has_email_match = true;
    }
  }

  // Skip if there aren't at least 1 strong match or 2 weak matches.
  if (!in_keyword_mode && strong_word_matches == 0 && weak_word_matches < 2) {
    return {0, strong_word_matches, weak_word_matches,
            "local, less than 1 strong or 2 weak word matches"};
  }

  // Skip when less than half the input words had matches. The backend
  // prioritizes high recall, whereas most omnibox suggestions require every
  // input word to match.
  if ((strong_word_matches + weak_word_matches) * 2 < input_words.size()) {
    return {0, strong_word_matches, weak_word_matches,
            "local, less than half the input words matched"};
  }

  // Compute `relevance` using text similarity. See comments for
  // `kMinWordsForFullTextMatchBoost` & `kScorePerStrongTextMatch`.
  CHECK_LE(kMaxTextScore(), kFullTextMatchScore());
  int relevance = 0;
  if (strong_word_matches == input_words.size() &&
      strong_word_matches >= kMinWordsForFullTextMatchBoost()) {
    relevance = kFullTextMatchScore();
  } else {
    relevance = std::min(
        static_cast<int>(strong_word_matches) * kScorePerStrongTextMatch() +
            static_cast<int>(weak_word_matches) * kScorePerWeakTextMatch(),
        kMaxTextScore());
  }

  // People suggestions must match every input word. Otherwise, they feel bad;
  // e.g. 'omnibox c' shouldn't suggest 'Charles Aznavour'. This doesn't apply
  // to `QUERY` and `CONTENT` types because those might have fuzzy matches or
  // matches within their contents.
  if (suggestion_type ==
      AutocompleteMatch::EnterpriseSearchAggregatorType::PEOPLE) {
    if (strong_word_matches + weak_word_matches < input_words.size()) {
      return {0, strong_word_matches, weak_word_matches,
              "local, unmatched input word for PEOPLE type"};
    } else {
      // See comment for `kPeopleEmailMatchScoreBoost`.
      if (has_email_match) {
        relevance += kPeopleEmailMatchScoreBoost();
      }
      // See comment for `kPeopleScoreBoost`.
      relevance += kPeopleScoreBoost();
    }
  }

  // See comment for `kPreferContentsOverQueries`.
  if (suggestion_type ==
          AutocompleteMatch::EnterpriseSearchAggregatorType::CONTENT &&
      kPreferContentsOverQueries()) {
    // 10 is small enough to not cause showing a worse CONTENT match over a
    // better non-CONTENT match.
    relevance += 10;
  }

  return {relevance, strong_word_matches, weak_word_matches, "local"};
}
}  // namespace

EnterpriseSearchAggregatorProvider::RequestParsed::RequestParsed() = default;
EnterpriseSearchAggregatorProvider::RequestParsed::RequestParsed(
    std::vector<AutocompleteMatch> matches,
    size_t result_count)
    : matches(std::move(matches)), result_count(result_count) {}

EnterpriseSearchAggregatorProvider::RequestParsed::~RequestParsed() = default;

EnterpriseSearchAggregatorProvider::RequestParsed::RequestParsed(
    RequestParsed&&) noexcept = default;

EnterpriseSearchAggregatorProvider::RequestParsed&
EnterpriseSearchAggregatorProvider::RequestParsed::operator=(
    RequestParsed&&) noexcept = default;

void EnterpriseSearchAggregatorProvider::RequestParsed::Append(
    RequestParsed parsed) {
  std::ranges::move(parsed.matches, std::back_inserter(matches));
  result_count += parsed.result_count;
}

EnterpriseSearchAggregatorProvider::Request::Request(
    std::vector<SuggestionType> types)
    : types_(types) {}

EnterpriseSearchAggregatorProvider::Request::~Request() = default;

EnterpriseSearchAggregatorProvider::Request::Request(Request&&) = default;

bool EnterpriseSearchAggregatorProvider::Request::Allowed(
    bool in_keyword_mode) const {
  // Query requests are only allowed in keyword mode.
  return !base::Contains(types_, SuggestionType::QUERY) || in_keyword_mode;
}

void EnterpriseSearchAggregatorProvider::Request::Reset(
    bool clear_cached_matches) {
  // If this request is interrupted, log its metrics now. Completed requests
  // will have already logged their metrics on completion.
  if (state_ == RequestState::kStarted) {
    Log(/*interrupted=*/true);
  }
  // Iff retaining cached matches, then this request is still allowed and is
  // expected to start.
  state_ = clear_cached_matches ? RequestState::kCompleted
                                : RequestState::kNotStarted;
  start_time_ = {};
  loader_.reset();
  // Don't clear `matches_` so old matches can be shown until the new response
  // is received and parsed.
  if (clear_cached_matches)
    parsed_ = {};
}

void EnterpriseSearchAggregatorProvider::Request::OnStart(
    std::unique_ptr<network::SimpleURLLoader> loader) {
  CHECK_EQ(state_, RequestState::kNotStarted);
  state_ = RequestState::kStarted;
  start_time_ = base::TimeTicks::Now();
  loader_ = std::move(loader);
}

void EnterpriseSearchAggregatorProvider::Request::OnCompleted(
    RequestParsed parsed) {
  CHECK_EQ(state_, RequestState::kStarted);
  state_ = RequestState::kCompleted;
  loader_.reset();
  parsed_ = std::move(parsed);
  Log(/*interrupted=*/false);
}

const std::vector<EnterpriseSearchAggregatorProvider::SuggestionType>
EnterpriseSearchAggregatorProvider::Request::Types() const {
  return types_;
}

std::vector<int>
EnterpriseSearchAggregatorProvider::Request::BackendSuggestionTypes() const {
  std::vector<int> backend_types = {};
  for (SuggestionType type : types_) {
    switch (type) {
      case SuggestionType::NONE:
        NOTREACHED();
      case SuggestionType::QUERY:
        backend_types.push_back(1);
        break;
      case SuggestionType::PEOPLE:
        backend_types.push_back(2);
        break;
      case SuggestionType::CONTENT:
        backend_types.push_back(3);
        backend_types.push_back(5);
        break;
    }
  }
  return backend_types;
}

EnterpriseSearchAggregatorProvider::RequestState
EnterpriseSearchAggregatorProvider::Request::State() const {
  return state_;
}

base::TimeTicks EnterpriseSearchAggregatorProvider::Request::StartTime() const {
  return start_time_;
}

const std::vector<AutocompleteMatch>&
EnterpriseSearchAggregatorProvider::Request::Matches() const {
  return parsed_.matches;
}

int EnterpriseSearchAggregatorProvider::Request::ResultCount() const {
  // Only completed requests log result counts.
  CHECK_EQ(state_, RequestState::kCompleted);
  return parsed_.result_count;
}

// static
void EnterpriseSearchAggregatorProvider::Request::LogResponseTime(
    const std::string& type_histogram_suffix,
    bool interrupted,
    base::TimeTicks start_time) {
  const std::string kResponseTimeHistogramName =
      "Omnibox.SuggestRequestsSent.ResponseTime2.RequestState";
  const std::string kEnterpriseRequestTypeString =
      "EnterpriseSearchAggregatorSuggest";

  const base::TimeDelta elapsed_time = base::TimeTicks::Now() - start_time;
  base::UmaHistogramTimes(
      base::StringPrintf("%s.%s%s.%s", kResponseTimeHistogramName,
                         kEnterpriseRequestTypeString, type_histogram_suffix,
                         interrupted ? "Interrupted" : "Completed"),
      elapsed_time);
  base::UmaHistogramTimes(
      base::StringPrintf("%s.%s%s", kResponseTimeHistogramName,
                         kEnterpriseRequestTypeString, type_histogram_suffix),
      elapsed_time);
}

// static
void EnterpriseSearchAggregatorProvider::Request::LogResultCount(
    const std::string& type_histogram_suffix,
    int count) {
  base::UmaHistogramExactLinear(
      base::StringPrintf("Omnibox.SuggestRequestsSent.ResultCount."
                         "EnterpriseSearchAggregatorSuggest%s",
                         type_histogram_suffix),
      count, 50);
}

void EnterpriseSearchAggregatorProvider::Request::Log(bool interrupted) const {
  // When making a single request, logging X.PEOPLE would be redundant with just
  // logging X.
  if (!kMultipleRequests())
    return;
  std::string suffix = TypeHistogramSuffix();
  LogResponseTime(suffix, interrupted, start_time_);
  // Only completed requests log result counts.
  if (!interrupted) {
    LogResultCount(suffix, parsed_.result_count);
  }
}

std::string EnterpriseSearchAggregatorProvider::Request::TypeHistogramSuffix()
    const {
  // Should not log type slices when making just a single request.
  CHECK_EQ(types_.size(), 1u);
  switch (types_[0]) {
    case EnterpriseSearchAggregatorProvider::SuggestionType::PEOPLE:
      return ".People";
    case EnterpriseSearchAggregatorProvider::SuggestionType::CONTENT:
      return ".Content";
    case EnterpriseSearchAggregatorProvider::SuggestionType::QUERY:
      return ".Query";
    case EnterpriseSearchAggregatorProvider::SuggestionType::NONE:
      NOTREACHED();
  }
}

EnterpriseSearchAggregatorProvider::EnterpriseSearchAggregatorProvider(
    AutocompleteProviderClient* client,
    AutocompleteProviderListener* listener)
    : AutocompleteProvider(
          AutocompleteProvider::TYPE_ENTERPRISE_SEARCH_AGGREGATOR),
      client_(client),
      debouncer_(std::make_unique<AutocompleteProviderDebouncer>(true, 300)),
      template_url_service_(client_->GetTemplateURLService()) {
  AddListener(listener);
  if (kMultipleRequests()) {
    requests_.push_back(Request{{SuggestionType::QUERY}});
    requests_.push_back(Request{{SuggestionType::PEOPLE}});
    requests_.push_back(Request{{SuggestionType::CONTENT}});
  } else {
    requests_.push_back(Request{{SuggestionType::QUERY, SuggestionType::PEOPLE,
                                 SuggestionType::CONTENT}});
  }
}

EnterpriseSearchAggregatorProvider::~EnterpriseSearchAggregatorProvider() =
    default;

void EnterpriseSearchAggregatorProvider::Start(const AutocompleteInput& input,
                                               bool minimal_changes) {
  // Don't clear matches. Keep showing old matches until a new response comes.
  // This avoids flickering.
  Stop(AutocompleteStopReason::kInteraction);

  if (!IsProviderAllowed(input)) {
    // Clear old matches if provider is not allowed.
    for (auto& request : requests_) {
      request.Reset(true);
    }
    matches_.clear();
    return;
  }

  // No need to redo or restart the previous request/response if the input
  // hasn't changed.
  if (minimal_changes) {
    return;
  }

  if (input.omit_asynchronous_matches()) {
    return;
  }

  adjusted_input_ = input;
  template_url_ = AdjustTemplateURL(&adjusted_input_, template_url_service_);
  CHECK(template_url_);
  CHECK(template_url_->policy_origin() ==
        TemplateURLData::PolicyOrigin::kSearchAggregator);

  // There should be no enterprise search suggestions fetched for on-focus
  // suggestion requests, or if the input is empty. Don't check
  // `OmniboxInputType::EMPTY` as the input's type isn't updated when keyword
  // adjusting.
  // TODO(crbug.com/393480150): Update this check once recent suggestions are
  //   supported.
  if (adjusted_input_.IsZeroSuggest() || adjusted_input_.text().empty()) {
    for (auto& request : requests_) {
      request.Reset(true);
    }
    matches_.clear();
    return;
  }

  done_ = false;  // Set true in callbacks.

  // Unretained is safe because `this` owns `debouncer_`.
  debouncer_->RequestRun(base::BindOnce(
      &EnterpriseSearchAggregatorProvider::Run, base::Unretained(this)));
}

void EnterpriseSearchAggregatorProvider::Stop(
    AutocompleteStopReason stop_reason) {
  // Ignore the stop timer since this provider is expected to sometimes take
  // longer than 1500ms.
  if (stop_reason == AutocompleteStopReason::kInactivity) {
    return;
  }
  AutocompleteProvider::Stop(stop_reason);
  debouncer_->CancelRequest();

  // If any requests haven't completed, then the type-unsliced histograms still
  // need to be logged. Otherwise, they were already logged when the last
  // request completed.
  if (std::any_of(requests_.begin(), requests_.end(), [](auto& request) {
        return request.State() == RequestState::kStarted;
      })) {
    LogAllRequests(true);
  }

  // Stop requests that haven't been started yet.
  if (auto* remote_suggestions_service = client_->GetRemoteSuggestionsService(
          /*create_if_necessary=*/false)) {
    remote_suggestions_service
        ->StopCreatingEnterpriseSearchAggregatorSuggestionsRequest();
  }

  // Stop ongoing requests but keep cached matches for ongoing and completed
  // requests.
  for (auto& request : requests_) {
    request.Reset(false);
  }
}

bool EnterpriseSearchAggregatorProvider::IsProviderAllowed(
    const AutocompleteInput& input) {
  // Don't start in incognito mode.
  if (client_->IsOffTheRecord()) {
    return false;
  }

  // Gate on "Improve Search Suggestions" setting.
  if (!client_->SearchSuggestEnabled()) {
    return false;
  }

  // There can be an aggregator set either through the feature params or through
  // a policy JSON. Both require this feature to be enabled.
  if (!omnibox_feature_configs::SearchAggregatorProvider::Get().enabled) {
    return false;
  }

  // Don't run provider in non-keyword mode if query length is less than
  // the minimum length.
  if (!input.InKeywordMode() &&
      static_cast<int>(input.text().length()) <
          omnibox_feature_configs::SearchAggregatorProvider::Get()
              .min_query_length) {
    return false;
  }

  // Don't run provider if the input is a URL.
  if (input.type() == metrics::OmniboxInputType::URL) {
    return false;
  }

  if (input.current_page_classification() ==
          metrics::OmniboxEventProto::NTP_REALBOX &&
      !omnibox_feature_configs::SearchAggregatorProvider::Get()
           .realbox_unscoped_suggestions) {
    return false;
  }

  // TODO(crbug.com/380642693): Add backoff check.
  return true;
}

void EnterpriseSearchAggregatorProvider::Run() {
  std::vector<int> request_indexes = {};
  std::vector<std::vector<int>> backend_suggestion_types = {};
  for (size_t i = 0; i < requests_.size(); ++i) {
    bool allowed = requests_[i].Allowed(adjusted_input_.InKeywordMode());
    requests_[i].Reset(!allowed);
    if (allowed) {
      request_indexes.push_back(i);
      backend_suggestion_types.push_back(requests_[i].BackendSuggestionTypes());
    }
  }

  // Necessary to update `matches_` immediately if e.g. the user just
  // entered/left keyword mode and query results should be removed/added.
  AggregateMatches();

  client_->GetRemoteSuggestionsService(/*create_if_necessary=*/true)
      ->CreateEnterpriseSearchAggregatorSuggestionsRequest(
          adjusted_input_.text(), GURL(template_url_->suggestions_url()),
          adjusted_input_.current_page_classification(), request_indexes,
          backend_suggestion_types,
          base::BindRepeating(
              &EnterpriseSearchAggregatorProvider::RequestStarted,
              weak_ptr_factory_.GetWeakPtr()),
          base::BindRepeating(
              &EnterpriseSearchAggregatorProvider::RequestCompleted,
              base::Unretained(this) /* this owns SimpleURLLoader */));
}

void EnterpriseSearchAggregatorProvider::RequestStarted(
    int request_index,
    std::unique_ptr<network::SimpleURLLoader> loader) {
  requests_[request_index].OnStart(std::move(loader));
}

void EnterpriseSearchAggregatorProvider::RequestCompleted(
    int request_index,
    const network::SimpleURLLoader* source,
    int response_code,
    std::unique_ptr<std::string> response_body) {
  DCHECK(!done_);
  DCHECK_GE(requests_.size(), static_cast<size_t>(request_index));

  if (response_code == 200) {
    // Parse `response_body` in utility process if feature param is true.
    const std::string& json_data = SearchSuggestionParser::ExtractJsonData(
        source, std::move(response_body));
    if (omnibox_feature_configs::SearchAggregatorProvider::Get()
            .parse_response_in_utility_process) {
      data_decoder::DataDecoder::ParseJsonIsolated(
          json_data,
          base::BindOnce(
              &EnterpriseSearchAggregatorProvider::OnJsonParsedIsolated,
              base::Unretained(this), request_index));
    } else {
      std::optional<base::Value::Dict> value = base::JSONReader::ReadDict(
          json_data, base::JSON_ALLOW_TRAILING_COMMAS);
      HandleParsedJson(request_index, value);
    }
  } else {
    HandleParsedJson(request_index, std::nullopt);
  }
}

void EnterpriseSearchAggregatorProvider::OnJsonParsedIsolated(
    int request_index,
    base::expected<base::Value, std::string> result) {
  std::optional<base::Value::Dict> value = std::nullopt;
  if (result.has_value() && result.value().is_dict()) {
    value = std::move(result.value().GetDict());
  }
  HandleParsedJson(request_index, value);
}

void EnterpriseSearchAggregatorProvider::HandleParsedJson(
    int request_index,
    const std::optional<base::Value::Dict>& response_value) {
  RequestParsed parsed =
      response_value.has_value()
          ? ParseEnterpriseSearchAggregatorSearchResults(
                requests_[request_index].Types(), response_value.value())
          : RequestParsed{};
  requests_[request_index].OnCompleted(std::move(parsed));

  AggregateMatches();
}

EnterpriseSearchAggregatorProvider::RequestParsed
EnterpriseSearchAggregatorProvider::
    ParseEnterpriseSearchAggregatorSearchResults(
        const std::vector<SuggestionType>& suggestion_types,
        const base::Value::Dict& root_val) {
  // Break the input into words to avoid redoing this for every match.
  std::set<std::u16string> input_words = GetWords({adjusted_input_.text()});

  // Parse the results.
  const base::Value::List* queryResults = root_val.FindList("querySuggestions");
  const base::Value::List* peopleResults =
      root_val.FindList("peopleSuggestions");
  const base::Value::List* contentResults =
      root_val.FindList("contentSuggestions");
  RequestParsed parsed{};
  if (base::Contains(suggestion_types, SuggestionType::QUERY)) {
    parsed.Append(ParseResultList(input_words, queryResults,
                                  /*suggestion_type=*/SuggestionType::QUERY,
                                  /*is_navigation=*/false));
  }
  if (base::Contains(suggestion_types, SuggestionType::PEOPLE)) {
    parsed.Append(ParseResultList(input_words, peopleResults,
                                  /*suggestion_type=*/SuggestionType::PEOPLE,
                                  /*is_navigation=*/true));
  }
  if (base::Contains(suggestion_types, SuggestionType::CONTENT)) {
    parsed.Append(ParseResultList(input_words, contentResults,
                                  /*suggestion_type=*/SuggestionType::CONTENT,
                                  /*is_navigation=*/true));
  }
  return parsed;
}

EnterpriseSearchAggregatorProvider::RequestParsed
EnterpriseSearchAggregatorProvider::ParseResultList(
    std::set<std::u16string> input_words,
    const base::Value::List* results,
    SuggestionType suggestion_type,
    bool is_navigation) {
  if (!results) {
    return {};
  }

  // Limit # of matches created. See comment for `kMaxMatchesCreatedPerType`.
  size_t num_results = std::min(results->size(), kMaxMatchesCreatedPerType());

  ACMatches matches;
  for (size_t i = 0; i < num_results; i++) {
    const base::Value& result_value = (*results)[i];
    if (!result_value.is_dict()) {
      continue;
    }

    const base::Value::Dict& result = result_value.GetDict();

    auto url = GetMatchDestinationUrl(result, suggestion_type);
    // All matches must have a URL.
    if (url.empty()) {
      continue;
    }

    // Some matches are supplied with an associated icon or image URL.
    std::string image_url;
    std::string icon_url;
    if (suggestion_type == SuggestionType::PEOPLE) {
      // For people suggestions, `icon_url` must always be set to the favicon
      // for the TemplateURL, which is used as the Omnibox icon. `image_url` is
      // used for the match icon, falling back to the favicon if not present.
      image_url = ptr_to_string(result.FindStringByDottedPath(
          "document.derivedStructData.displayPhoto.url"));
      // Ensure that image URLs from lh3.googleusercontent.com include an image
      // size parameter.
      if (base::StartsWith(image_url, "https://lh3.googleusercontent.com")) {
        // Check for existing size parameters (e.g., -s128, =w256, -h64).
        RE2 size_regex("=(?:[swh]\\d+|[^=]*?-[swh]\\d+)");
        if (!RE2::PartialMatch(image_url, size_regex)) {
          image_url += base::Contains(image_url, "=") ? "-s64" : "=s64";
        }
      }
      icon_url = template_url_->favicon_url().spec();
    } else if (suggestion_type == SuggestionType::CONTENT) {
      icon_url = ptr_to_string(result.FindString("iconUri"));
    } else if (suggestion_type == SuggestionType::QUERY &&
               !adjusted_input_.InKeywordMode()) {
      icon_url = template_url_->favicon_url().spec();
    }

    auto description = GetMatchDescription(result, suggestion_type);
    // Nav matches must have a description.
    if (is_navigation && description.empty()) {
      continue;
    }

    auto contents = GetMatchContents(result, suggestion_type);
    // Search matches must have contents.
    if (!is_navigation && contents.empty()) {
      continue;
    }

    EnterpriseSearchAggregatorProvider::RelevanceData relevance_data;
    std::string relevance_scoring_mode =
        omnibox_feature_configs::SearchAggregatorProvider::Get()
            .relevance_scoring_mode;
    // If mode is `server|client`, always use server|client scoring; otherwise,
    // use server scoring in scoped mode, and client scoring in unscoped mode.
    if (relevance_scoring_mode == "server" ||
        (relevance_scoring_mode != "client" &&
         adjusted_input_.InKeywordMode())) {
      relevance_data = GetServerRelevanceData(result);
    } else {
      const std::vector<std::u16string> email_usernames =
          GetEmailUsernames(result);
      auto strong_scoring_fields = GetStrongScoringFields(
          result, suggestion_type, contents, description, email_usernames);
      auto weak_scoring_fields = GetWeakScoringFields(result, suggestion_type);
      relevance_data = CalculateRelevanceData(
          input_words, adjusted_input_.InKeywordMode(), suggestion_type,
          strong_scoring_fields, weak_scoring_fields, email_usernames);
    }
    if (relevance_data.relevance) {
      // Decrement scores to keep sorting stable. Add 10 to avoid going below
      // "weak" threshold or change the hundred's digit; e.g. a score of
      // 600 v 599 could drastically affect the match's omnibox ranking.
      relevance_data.relevance += 10 - matches.size();
    }

    std::u16string fill_into_edit;
    if (adjusted_input_.InKeywordMode()) {
      fill_into_edit.append(template_url_->keyword() + u' ');
    }
    fill_into_edit.append(base::UTF8ToUTF16(is_navigation ? url : contents));

    matches.push_back(CreateMatch(suggestion_type, is_navigation,
                                  relevance_data, url, image_url, icon_url,
                                  base::UTF8ToUTF16(description),
                                  base::UTF8ToUTF16(contents), fill_into_edit));
  }

  // Limit # of matches added. See comment for
  // `kMaxScopedMatchesShownPerType`.
  size_t matches_to_add = adjusted_input_.InKeywordMode()
                              ? kMaxScopedMatchesShownPerType()
                              : kMaxUnscopedMatchesShownPerType();
  if (matches_to_add < matches.size()) {
    std::ranges::partial_sort(matches, matches.begin() + matches_to_add,
                              std::ranges::greater{},
                              &AutocompleteMatch::relevance);
    matches.erase(matches.begin() + matches_to_add, matches.end());
  }

  return {std::move(matches), results->size()};
}

std::string EnterpriseSearchAggregatorProvider::GetMatchDestinationUrl(
    const base::Value::Dict& result,
    SuggestionType suggestion_type) const {
  std::string destination_uri =
      ptr_to_string(result.FindString("destinationUri"));
  if (suggestion_type == SuggestionType::CONTENT ||
      suggestion_type == SuggestionType::PEOPLE) {
    return destination_uri;
  }

  std::string query = ptr_to_string(result.FindString("suggestion"));
  if (query.empty()) {
    return "";
  }

  const TemplateURLRef& url_ref = template_url_->url_ref();
  return url_ref.ReplaceSearchTerms(
      TemplateURLRef::SearchTermsArgs(base::UTF8ToUTF16(query)), {}, nullptr);
}

std::string EnterpriseSearchAggregatorProvider::GetMatchDescription(
    const base::Value::Dict& result,
    SuggestionType suggestion_type) const {
  if (suggestion_type == SuggestionType::PEOPLE) {
    return ptr_to_string(result.FindStringByDottedPath(
        "document.derivedStructData.name.displayName"));
  } else if (suggestion_type == SuggestionType::CONTENT) {
    return ptr_to_string(
        result.FindStringByDottedPath("document.derivedStructData.title"));
  }
  return "";
}

std::string EnterpriseSearchAggregatorProvider::GetMatchContents(
    const base::Value::Dict& result,
    SuggestionType suggestion_type) const {
  if (suggestion_type == SuggestionType::QUERY) {
    return ptr_to_string(result.FindString("suggestion"));
  } else if (suggestion_type == SuggestionType::PEOPLE) {
    return l10n_util::GetStringFUTF8(
        IDS_PERSON_SUGGESTION_DESCRIPTION,
        template_url_->AdjustedShortNameForLocaleDirection());
  } else if (suggestion_type == SuggestionType::CONTENT) {
    std::optional<int> response_time =
        result.FindIntByDottedPath("document.derivedStructData.updated_time");
    const std::u16string last_updated = UpdateTimeToString(response_time);

    const std::u16string owner = base::UTF8ToUTF16(ptr_to_string(
        result.FindStringByDottedPath("document.derivedStructData.owner")));

    const std::u16string content_type_description = ContentTypeToDescription(
        ptr_to_string(result.FindStringByDottedPath(
            "document.derivedStructData.mime_type")),
        ptr_to_string(result.FindStringByDottedPath(
            "document.derivedStructData.source_type")));

    return base::UTF16ToUTF8(GetLocalizedContentMetadata(
        last_updated, owner, content_type_description));
  }

  return "";
}

std::u16string EnterpriseSearchAggregatorProvider::GetLocalizedContentMetadata(
    const std::u16string& update_time,
    const std::u16string& owner,
    const std::u16string& content_type_description) const {
  if (!update_time.empty()) {
    if (!owner.empty()) {
      return !content_type_description.empty()
                 ? l10n_util::GetStringFUTF16(
                       IDS_CONTENT_SUGGESTION_DESCRIPTION_TEMPLATE, update_time,
                       owner, content_type_description)
                 : l10n_util::GetStringFUTF16(
                       IDS_CONTENT_SUGGESTION_DESCRIPTION_TEMPLATE_WITHOUT_FILE_TYPE_DESCRIPTION,
                       update_time, owner);
    }
    return !content_type_description.empty()
               ? l10n_util::GetStringFUTF16(
                     IDS_CONTENT_SUGGESTION_DESCRIPTION_TEMPLATE_WITHOUT_OWNER,
                     update_time, content_type_description)
               : update_time;
  }
  if (!owner.empty()) {
    return !content_type_description.empty()
               ? l10n_util::GetStringFUTF16(
                     IDS_CONTENT_SUGGESTION_DESCRIPTION_TEMPLATE_WITHOUT_DATE,
                     owner, content_type_description)
               : owner;
  }
  return !content_type_description.empty() ? content_type_description : u"";
}

std::vector<std::string>
EnterpriseSearchAggregatorProvider::GetStrongScoringFields(
    const base::Value::Dict& result,
    SuggestionType suggestion_type,
    const std::string& contents,
    const std::string& description,
    const std::vector<std::u16string> email_usernames) const {
  std::vector<std::string> strong_scoring_fields;
  // Should not return any fields already included in `GetMatchDescription()` &
  // `GetMatchContents()`.
  if (suggestion_type == SuggestionType::PEOPLE) {
    std::ranges::transform(
        email_usernames, std::back_inserter(strong_scoring_fields),
        [](const auto& u16string) { return base::UTF16ToUTF8(u16string); });
  } else {
    // Contents field for people suggestions is always "{NAME} People" and is
    // not a good field to use to score relevancy.
    strong_scoring_fields.push_back(contents);
  }
  strong_scoring_fields.push_back(description);
  return strong_scoring_fields;
}

std::vector<std::string>
EnterpriseSearchAggregatorProvider::GetWeakScoringFields(
    const base::Value::Dict& result,
    SuggestionType suggestion_type) const {
  // Should not return any fields already included in `GetMatchDescription()` &
  // `GetMatchContents()`.
  if (suggestion_type == SuggestionType::PEOPLE) {
    return {
        ptr_to_string(result.FindStringByDottedPath(
            "document.derivedStructData.name.givenName")),
        ptr_to_string(result.FindStringByDottedPath(
            "document.derivedStructData.name.familyName")),
    };
  } else if (suggestion_type == SuggestionType::CONTENT) {
    return {
        ptr_to_string(
            result.FindStringByDottedPath("document.derivedStructData.owner")),
        ptr_to_string(result.FindStringByDottedPath(
            "document.derivedStructData.mime_type")),
        ptr_to_string(result.FindStringByDottedPath(
            "document.derivedStructData.owner_email")),
    };
  }
  return {};
}

AutocompleteMatch EnterpriseSearchAggregatorProvider::CreateMatch(
    SuggestionType suggestion_type,
    bool is_navigation,
    RelevanceData relevance_data,
    const std::string& url,
    const std::string& image_url,
    const std::string& icon_url,
    const std::u16string& description,
    const std::u16string& contents,
    const std::u16string& fill_into_edit) {
  auto type = is_navigation ? AutocompleteMatchType::NAVSUGGEST
                            : AutocompleteMatchType::SEARCH_SUGGEST;
  AutocompleteMatch match(this, relevance_data.relevance, false, type);

  match.destination_url = GURL(url);

  if (!image_url.empty()) {
    match.image_url = GURL(image_url);
  }

  if (!icon_url.empty()) {
    match.icon_url = GURL(icon_url);
  }

  match.enterprise_search_aggregator_type = suggestion_type;
  match.description = AutocompleteMatch::SanitizeString(description);
  match.contents = AutocompleteMatch::SanitizeString(contents);
  if (!is_navigation) {
    match.search_terms_args =
        std::make_unique<TemplateURLRef::SearchTermsArgs>(match.contents);
  }

  // `NAVSUGGEST` is displayed "<description> - <contents>" and
  // `SEARCH_SUGGEST` is displayed "<contents> - <description>".
  // The below code formats `description` and `contents` accordingly.
  auto primary_text_class = [&](auto text) {
    return ClassifyTermMatches(FindTermMatches(adjusted_input_.text(), text),
                               text.size(), ACMatchClassification::MATCH,
                               ACMatchClassification::NONE);
  };
  ACMatchClassifications secondary_text_class =
      (contents.empty() || description.empty())
          ? std::vector<ACMatchClassification>{}
          : std::vector<ACMatchClassification>{{0, ACMatchClassification::DIM}};
  match.description_class = is_navigation
                                ? primary_text_class(match.description)
                                : secondary_text_class;
  match.contents_class =
      is_navigation ? secondary_text_class : primary_text_class(match.contents);
  match.fill_into_edit = fill_into_edit;

  match.keyword = template_url_->keyword();
  match.transition = adjusted_input_.InKeywordMode()
                         ? ui::PAGE_TRANSITION_KEYWORD
                         : ui::PAGE_TRANSITION_GENERATED;

  if (adjusted_input_.InKeywordMode()) {
    match.from_keyword = true;
  }

  match.RecordAdditionalInfo("aggregator type",
                             static_cast<int>(suggestion_type));
  match.RecordAdditionalInfo(
      "relevance strong word matches",
      static_cast<int>(relevance_data.strong_word_matches));
  match.RecordAdditionalInfo(
      "relevance weak word matches",
      static_cast<int>(relevance_data.weak_word_matches));
  match.RecordAdditionalInfo("relevance source", relevance_data.source);

  return match;
}

void EnterpriseSearchAggregatorProvider::AggregateMatches() {
  // Aggregate matches from `requests_` to `matches_`.
  matches_.clear();
  for (auto& request : requests_) {
    std::ranges::copy(request.Matches(), std::back_inserter(matches_));
  }

  // Limit low-quality suggestions. See comment for
  // `kScopedMaxLowQualityMatches`.
  std::ranges::sort(matches_, std::ranges::greater{},
                    &AutocompleteMatch::relevance);
  size_t matches_to_keep = adjusted_input_.InKeywordMode()
                               ? kScopedMaxLowQualityMatches()
                               : kUnscopedMaxLowQualityMatches();
  if (matches_.size() > matches_to_keep) {
    for (; matches_to_keep < matches_.size(); ++matches_to_keep) {
      if (matches_[matches_to_keep].relevance < kLowQualityThreshold()) {
        break;
      }
    }
    matches_.erase(matches_.begin() + matches_to_keep, matches_.end());
  }

  // If all requests completed, then log the type-unsliced histograms.
  if (std::all_of(requests_.begin(), requests_.end(), [](auto& request) {
        return request.State() == RequestState::kCompleted;
      })) {
    LogAllRequests(false);
    done_ = true;
  }

  NotifyListeners(/*updated_matches=*/true);
}

void EnterpriseSearchAggregatorProvider::LogAllRequests(bool interrupted) {
  base::TimeTicks earliest_start_time =
      std::ranges::min_element(requests_, {}, &Request::StartTime)->StartTime();
  Request::LogResponseTime(/*type_histogram_suffix=*/"", interrupted,
                           earliest_start_time);

  // Only completed requests log result counts.
  if (!interrupted) {
    int total_result_count = 0;
    for (auto& request : requests_) {
      total_result_count += request.ResultCount();
    }
    Request::LogResultCount(/*type_histogram_suffix=*/"", total_result_count);
  }
}
