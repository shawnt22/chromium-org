// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PDF_PDFIUM_PDFIUM_ON_DEMAND_SEARCHIFIER_H_
#define PDF_PDFIUM_PDFIUM_ON_DEMAND_SEARCHIFIER_H_

#include <optional>
#include <vector>

#include "base/containers/circular_deque.h"
#include "base/functional/callback_forward.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "pdf/pdfium/pdfium_engine.h"
#include "services/screen_ai/public/mojom/screen_ai_service.mojom.h"
#include "third_party/pdfium/public/cpp/fpdf_scopers.h"
#include "third_party/skia/include/core/SkBitmap.h"
namespace chrome_pdf {

class PDFiumOnDemandSearchifier {
 public:
  explicit PDFiumOnDemandSearchifier(PDFiumEngine* engine);
  ~PDFiumOnDemandSearchifier();

  // Starts performing searchify on the scheduled pages. The function should be
  // called only once. If pages are added for searchifying later, they are
  // automatically picked up from the queue.
  void Start(GetOcrMaxImageDimensionCallbackAsync get_max_dimension_callback,
             PerformOcrCallbackAsync perform_ocr_callback);

  // Called when OCR service is disconnected and is not available anymore.
  void OnOcrDisconnected();

  // Checks if the page is queued to be searchified or the searchifying process
  // has started for it but not finished yet.
  bool IsPageScheduled(int page_index) const;

  // Puts a page in the queue to be searchified. This function can be called
  // before `Start` and if so, the page stays in the queue until searchifier
  // starts.
  void SchedulePage(int page_index);

  bool HasFailed() const { return state_ == State::kFailed; }
  bool IsIdleForTesting() const { return state_ == State::kIdle; }

 private:
  enum class State { kIdle, kWaitingForResults, kFailed };

  void SearchifyNextPage();
  void SearchifyNextImage();

  void CommitResultsToPage();

  struct BitmapResult {
    SkBitmap bitmap;
    int image_index;
  };

  struct OcrResult {
    OcrResult(int image_index,
              screen_ai::mojom::VisualAnnotationPtr annotation,
              const gfx::Size& image_size);
    OcrResult(const OcrResult& other) = delete;
    OcrResult(OcrResult&& other) noexcept;
    ~OcrResult();

    int image_index = 0;
    screen_ai::mojom::VisualAnnotationPtr annotation;
    gfx::Size image_size;
  };

  void OnGotOcrMaxImageDimension(uint32_t max_image_dimension);

  std::optional<BitmapResult> GetNextBitmap();
  void OnGotOcrResult(int image_index,
                      const gfx::Size& image_size,
                      screen_ai::mojom::VisualAnnotationPtr annotation);

  // Owns this class.
  const raw_ref<PDFiumEngine> engine_;

  ScopedFPDFFont font_;

  // Callback function to get max resolution. Should be used only once.
  GetOcrMaxImageDimensionCallbackAsync get_max_dimension_callback_;

  // Callback function to perform OCR.
  PerformOcrCallbackAsync perform_ocr_callback_;

  // Maximum dimension size for images to be sent to OCR. This value is updated
  // after OCR service is connected and stored for subsequent calls.
  // OCR service downsamples images before processing if their dimensions are
  // above this threshold. Sending larger images has processing and memory
  // overhead and does not have any other negative effect.
  uint32_t max_image_dimension_ = 0;

  // The page that is currently OCRed.
  raw_ptr<PDFiumPage> current_page_ = nullptr;
  bool current_page_was_loaded_ = false;
  std::vector<int> current_page_image_object_indices_;
  std::vector<OcrResult> current_page_ocr_results_;

  // Scheduled pages to be searchified.
  base::circular_deque<int> pages_queue_;

  State state_ = State::kIdle;

  base::WeakPtrFactory<PDFiumOnDemandSearchifier> weak_factory_{this};
};

}  // namespace chrome_pdf

#endif  // PDF_PDFIUM_PDFIUM_ON_DEMAND_SEARCHIFIER_H_
