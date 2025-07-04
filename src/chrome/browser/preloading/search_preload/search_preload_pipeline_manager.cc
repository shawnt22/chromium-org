// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/preloading/search_preload/search_preload_pipeline_manager.h"

#include "chrome/browser/preloading/chrome_preloading.h"
#include "chrome/browser/preloading/prefetch/search_prefetch/search_prefetch_service.h"
#include "chrome/browser/preloading/search_preload/search_preload_features.h"
#include "chrome/browser/preloading/search_preload/search_preload_pipeline.h"
#include "chrome/browser/preloading/search_preload/search_preload_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/omnibox/browser/autocomplete_result.h"
#include "components/omnibox/browser/base_search_provider.h"
#include "components/omnibox/browser/omnibox.mojom-shared.h"
#include "components/search_engines/template_url_service.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/preloading_data.h"
#include "content/public/browser/web_contents.h"

namespace {

// Ergonomic wrapper of `HasCanonicalPreloadingOmniboxSearchURL()`
std::optional<GURL> GetCanonicalUrlForSearchPreload(
    content::BrowserContext& browser_context,
    const GURL& preload_url) {
  GURL canonical_url;
  if (HasCanonicalPreloadingOmniboxSearchURL(preload_url, &browser_context,
                                             &canonical_url)) {
    return canonical_url;
  }

  return std::nullopt;
}

// Ergonomic wrapper of `ExtractSearchTermsFromURL()`
std::optional<std::u16string> ExtractSearchTermsFromUrl(
    TemplateURLService& template_url_service,
    const AutocompleteMatch& match) {
  std::u16string search_terms;
  if (template_url_service.GetDefaultSearchProvider()
          ->ExtractSearchTermsFromURL(match.destination_url,
                                      template_url_service.search_terms_data(),
                                      &search_terms)) {
    return search_terms;
  }
  return std::nullopt;
}

}  // namespace

WEB_CONTENTS_USER_DATA_KEY_IMPL(SearchPreloadPipelineManager);

SearchPreloadPipelineManager::SearchPreloadPipelineManager(
    content::WebContents* web_contents)
    : content::WebContentsUserData<SearchPreloadPipelineManager>(*web_contents),
      content::WebContentsObserver(web_contents) {
  auto* preloading_data =
      content::PreloadingData::GetOrCreateForWebContents(web_contents);
  SetIsNavigationInDomainCallback(preloading_data);
}

SearchPreloadPipelineManager::~SearchPreloadPipelineManager() = default;

void SearchPreloadPipelineManager::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  const bool is_primary_main_frame_navigation =
      navigation_handle->HasCommitted() &&
      navigation_handle->IsInPrimaryMainFrame() &&
      !navigation_handle->IsSameDocument();
  if (!is_primary_main_frame_navigation) {
    return;
  }

  content::BrowserContext* browser_context =
      GetWebContents().GetBrowserContext();
  if (!browser_context) {
    return;
  }

  // Invalidate a pipeline if it is likely used.
  std::optional<GURL> maybe_canonical_url = GetCanonicalUrlForSearchPreload(
      *browser_context, navigation_handle->GetURL());
  if (!maybe_canonical_url.has_value()) {
    return;
  }
  const GURL& canonical_url = maybe_canonical_url.value();

  pipelines_.erase(canonical_url);
}

void SearchPreloadPipelineManager::ClearPreloads() {
  pipelines_.clear();
}

void SearchPreloadPipelineManager::EraseNotAlivePipelines() {
  base::EraseIf(
      pipelines_,
      [](const std::pair<GURL, std::unique_ptr<SearchPreloadPipeline>>& pair) {
        auto& pipeline = pair.second;
        const bool is_alive =
            pipeline->IsPrefetchAlive() || pipeline->IsPrerenderValid();
        return !is_alive;
      });
}

void SearchPreloadPipelineManager::OnAutocompleteResultChanged(
    Profile& profile,
    base::WeakPtr<SearchPreloadService> search_preload_service,
    const AutocompleteResult& result,
    const std::optional<net::HttpNoVarySearchData>& no_vary_search_hint) {
  auto* template_url_service =
      TemplateURLServiceFactory::GetForProfile(&profile);
  CHECK(template_url_service);
  if (!template_url_service->GetDefaultSearchProvider()) {
    return;
  }

  // Erase to count prefetches.
  EraseNotAlivePipelines();

  if (base::FeatureList::IsEnabled(
          features::kDsePreload2OnSuggestNonDefalutMatch)) {
    for (const auto& match : result) {
      // Limit the number of prefetches.
      if (pipelines_.size() >= features::kDsePreload2MaxPrefetch.Get()) {
        return;
      }

      OnAutocompleteResultChangedProcessOne(profile, search_preload_service,
                                            *template_url_service, match,
                                            no_vary_search_hint);
    }
  } else {
    if (!result.default_match()) {
      return;
    }
    const auto& match = *result.default_match();

    // Limit the number of prefetches.
    if (pipelines_.size() >= features::kDsePreload2MaxPrefetch.Get()) {
      return;
    }

    OnAutocompleteResultChangedProcessOne(profile, search_preload_service,
                                          *template_url_service, match,
                                          no_vary_search_hint);
  }
}

void SearchPreloadPipelineManager::OnAutocompleteResultChangedProcessOne(
    Profile& profile,
    base::WeakPtr<SearchPreloadService> search_preload_service,
    TemplateURLService& template_url_service,
    const AutocompleteMatch& match,
    const std::optional<net::HttpNoVarySearchData>& no_vary_search_hint) {
  const bool should_prefetch = BaseSearchProvider::ShouldPrefetch(match) ||
                               BaseSearchProvider::ShouldPrerender(match);
  const bool should_prerender = BaseSearchProvider::ShouldPrerender(match);

  // In the case of Default Search Engine Prediction, the confidence depends
  // on the type of preload. For prerender requests, the confidence is
  // comparatively higher than the prefetch to avoid the impact of wrong
  // predictions. We set confidence as 80 for prerender matches and 60 for
  // prefetch as an approximate number to differentiate both these cases.
  //
  // The value is used only for precog. So, these values have no concreate
  // meanings.
  int confidence;
  if (should_prerender) {
    confidence = 80;
  } else if (should_prefetch) {
    confidence = 60;
  } else {
    return;
  }

  std::optional<GURL> maybe_canonical_url =
      GetCanonicalUrlForSearchPreload(profile, match.destination_url);
  if (!maybe_canonical_url.has_value()) {
    return;
  }
  const GURL& canonical_url = maybe_canonical_url.value();

  // TODO(crbug.com/403198750): Limit the number of active pipelines.
  if (!pipelines_.contains(canonical_url)) {
    pipelines_.insert_or_assign(
        canonical_url, std::make_unique<SearchPreloadPipeline>(canonical_url));
  }
  pipelines_[canonical_url]->UpdateConfidence(GetWebContents(), confidence);

  CHECK(should_prefetch);
  const GURL prefetch_url =
      GetPrefetchUrlFromMatch(*match.search_terms_args, template_url_service,
                              /*is_navigation_likely=*/false);
  pipelines_[canonical_url]->StartPrefetch(
      GetWebContents(), search_preload_service, prefetch_url,
      chrome_preloading_predictor::kDefaultSearchEngine, no_vary_search_hint,
      /*is_navigation_likely=*/false);

  // Trigger prerender without waiting prefetch.
  //
  // They are coordinated by `PrefetchMatchResolver`. For more details, see
  // https://docs.google.com/document/d/1IAIVrDBE-FnO14Qnghr8hsrxUeoFfeob5QIsV_UNRck/edit?tab=t.0#heading=h.vpxgrp4zne09
  if (should_prerender) {
    // Unlike prefetch, we cancel the existing prerender and start new one if we
    // have a signal for prerender. This behavior comes from DSE preload 1
    // (`SearchPrefetchService`).
    //
    // TODO(https://crrev.com/421387697): Consider to use different policy.
    for (const auto& [key, value] : pipelines_) {
      if (key != canonical_url) {
        value->CancelPrerender();
      }
    }

    const GURL prerender_url = GetPrerenderUrlFromMatch(
        *match.search_terms_args, template_url_service);
    pipelines_[canonical_url]->StartPrerender(
        GetWebContents(), prerender_url,
        chrome_preloading_predictor::kDefaultSearchEngine);
  }
}

bool SearchPreloadPipelineManager::OnNavigationLikely(
    Profile& profile,
    base::WeakPtr<SearchPreloadService> search_preload_service,
    const AutocompleteMatch& match,
    omnibox::mojom::NavigationPredictor navigation_predictor,
    const std::optional<net::HttpNoVarySearchData>& no_vary_search_hint) {
  if (!features::IsDsePreload2OnPressEnabled()) {
    return false;
  }

  if (!features::DsePreload2OnPressIsPredictorEnabled(navigation_predictor)) {
    return false;
  }

  if (profile.IsOffTheRecord() &&
      !features::IsDsePreload2OnPressIncognitoEnabled()) {
    return false;
  }

  if (!AutocompleteMatch::IsSearchType(match.type)) {
    return false;
  }

  auto* template_url_service =
      TemplateURLServiceFactory::GetForProfile(&profile);
  CHECK(template_url_service);
  bool does_search_provider_opt_in =
      template_url_service->GetDefaultSearchProvider() &&
      template_url_service->GetDefaultSearchProvider()
          ->data()
          .prefetch_likely_navigations;
  if (!does_search_provider_opt_in) {
    return false;
  }

  // Erase to count prefetches.
  EraseNotAlivePipelines();
  // Limit the number of prefetches.
  if (pipelines_.size() >= features::kDsePreload2MaxPrefetch.Get()) {
    return false;
  }

  const std::optional<GURL> maybe_canonical_url =
      GetCanonicalUrlForSearchPreload(profile, match.destination_url);
  if (!maybe_canonical_url.has_value()) {
    return false;
  }
  const GURL& canonical_url = maybe_canonical_url.value();

  const std::optional<std::u16string> maybe_search_terms =
      ExtractSearchTermsFromUrl(*template_url_service, match);
  if (!maybe_search_terms.has_value()) {
    return false;
  }
  const std::u16string& search_terms = maybe_search_terms.value();

  GURL prefetch_url;
  if (match.search_terms_args) {
    auto& search_terms_args = *match.search_terms_args.get();
    prefetch_url =
        GetPrefetchUrlFromMatch(search_terms_args, *template_url_service,
                                /*is_navigation_likely=*/true);
  } else {
    // Search history suggestions (those that are not also server suggestions)
    // don't have search term args. Generate search term args instead.

    auto search_terms_args_for_history_suggestion =
        std::make_unique<TemplateURLRef::SearchTermsArgs>(search_terms);
    auto& search_terms_args = *search_terms_args_for_history_suggestion.get();
    prefetch_url =
        GetPrefetchUrlFromMatch(search_terms_args, *template_url_service,
                                /*is_navigation_likely=*/true);
  }

  auto predictor =
      [](omnibox::mojom::NavigationPredictor navigation_predictor) {
        switch (navigation_predictor) {
          case omnibox::mojom::NavigationPredictor::kMouseDown:
            return chrome_preloading_predictor::kOmniboxMousePredictor;
          case omnibox::mojom::NavigationPredictor::kUpOrDownArrowButton:
            return chrome_preloading_predictor::kOmniboxSearchPredictor;
          case omnibox::mojom::NavigationPredictor::kTouchDown:
            return chrome_preloading_predictor::kOmniboxTouchDownPredictor;
        }
      }(navigation_predictor);

  // TODO(crbug.com/403198750): Limit the number of active pipelines.
  if (!pipelines_.contains(canonical_url)) {
    pipelines_.insert_or_assign(
        canonical_url, std::make_unique<SearchPreloadPipeline>(canonical_url));
  }
  pipelines_[canonical_url]->UpdateConfidence(GetWebContents(), 100);
  return pipelines_[canonical_url]->StartPrefetch(
      GetWebContents(), search_preload_service, prefetch_url, predictor,
      no_vary_search_hint,
      /*is_navigation_likely=*/true);
}

bool SearchPreloadPipelineManager::InvalidatePipelineForTesting(
    GURL canonical_url) {
  return static_cast<bool>(pipelines_.erase(canonical_url));
}
