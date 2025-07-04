// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_AUTOFILL_ANDROID_AUTOFILL_IMAGE_FETCHER_IMPL_H_
#define CHROME_BROWSER_AUTOFILL_ANDROID_AUTOFILL_IMAGE_FETCHER_IMPL_H_

#include "base/android/scoped_java_ref.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/profiles/profile_key.h"
#include "components/autofill/core/browser/ui/autofill_image_fetcher_base.h"
#include "components/keyed_service/core/keyed_service.h"

namespace autofill {

// chrome/ implementation of AutofillImageFetcherBase for Android clients.
class AutofillImageFetcherImpl : public AutofillImageFetcherBase,
                                 public KeyedService {
 public:
  explicit AutofillImageFetcherImpl(ProfileKey* key);
  AutofillImageFetcherImpl(const AutofillImageFetcherImpl&) = delete;
  AutofillImageFetcherImpl& operator=(const AutofillImageFetcherImpl&) = delete;
  ~AutofillImageFetcherImpl() override;

  // AutofillImageFetcherBase:
  void FetchCreditCardArtImagesForURLs(
      base::span<const GURL> image_urls,
      base::span<const AutofillImageFetcherBase::ImageSize> image_sizes)
      override;
  void FetchPixAccountImagesForURLs(base::span<const GURL> image_urls) override;
  void FetchValuableImagesForURLs(base::span<const GURL> image_urls) override;
  const gfx::Image* GetCachedImageForUrl(const GURL& image_url,
                                         ImageType image_type) const override;
  base::android::ScopedJavaLocalRef<jobject> GetOrCreateJavaImageFetcher()
      override;

 private:
  raw_ptr<ProfileKey> key_;

  base::android::ScopedJavaGlobalRef<jobject> java_image_fetcher_;
};

}  // namespace autofill

#endif  // CHROME_BROWSER_AUTOFILL_ANDROID_AUTOFILL_IMAGE_FETCHER_IMPL_H_
