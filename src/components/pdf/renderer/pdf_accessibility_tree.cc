// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/pdf/renderer/pdf_accessibility_tree.h"

#include <algorithm>
#include <iterator>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "base/check_is_test.h"
#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/location.h"
#include "base/memory/ptr_util.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "base/metrics/histogram_functions.h"
#include "base/notreached.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "components/pdf/renderer/pdf_accessibility_tree_builder.h"
#include "components/pdf/renderer/pdf_ax_action_target.h"
#include "components/strings/grit/components_strings.h"
#include "content/public/renderer/render_accessibility.h"
#include "content/public/renderer/render_frame.h"
#include "content/public/renderer/render_thread.h"
#include "pdf/pdf_accessibility_action_handler.h"
#include "third_party/blink/public/web/web_element.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_plugin_container.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/accessibility/ax_enums.mojom-shared.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_mode.h"
#include "ui/accessibility/ax_node_id_forward.h"
#include "ui/accessibility/ax_tree_id.h"
#include "ui/accessibility/ax_tree_update.h"
#include "ui/accessibility/null_ax_action_target.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/transform.h"

#if BUILDFLAG(ENABLE_SCREEN_AI_SERVICE)
#include "base/containers/contains.h"
#include "ui/strings/grit/auto_image_annotation_strings.h"
#endif  // BUILDFLAG(ENABLE_SCREEN_AI_SERVICE)

namespace pdf {

namespace ranges = std::ranges;

namespace {

// Delay before loading all the PDF content into the accessibility tree and
// resetting the banner and status nodes in an accessibility tree.
constexpr base::TimeDelta kDelayBeforeResettingStatusNode = base::Seconds(1);

enum class AttributeUpdateType {
  kRemove = 0,
  kAdd,
};

ui::AXNode* GetStaticTextNodeFromNode(ui::AXNode* node) {
  // Returns the appropriate static text node given `node`'s type.
  // Returns nullptr if there is no appropriate static text node.
  if (!node)
    return nullptr;
  ui::AXNode* static_node = node;
  // Get the static text from the link node.
  if (node->GetRole() == ax::mojom::Role::kLink &&
      node->children().size() == 1) {
    static_node = node->children()[0];
  }
  // Get the static text from the highlight node.
  if (node->GetRole() == ax::mojom::Role::kPdfActionableHighlight &&
      !node->children().empty()) {
    static_node = node->children()[0];
  }
  // If it's static text node, then it holds text.
  if (static_node && static_node->GetRole() == ax::mojom::Role::kStaticText)
    return static_node;
  return nullptr;
}

template <typename T>
bool CompareTextRuns(const T& a, const T& b) {
  return a.text_run_index < b.text_run_index;
}

template <typename T>
bool CompareTextRunsWithRange(const T& a, const T& b) {
  return a.text_range.index < b.text_range.index;
}

std::unique_ptr<ui::AXNodeData> CreateNode(ax::mojom::Role role,
                                           ax::mojom::Restriction restriction,
                                           ui::AXNodeID id) {
  auto node = std::make_unique<ui::AXNodeData>();
  node->id = id;
  node->role = role;
  node->SetRestriction(restriction);

  return node;
}

void UpdateStatusNodeLiveRegionAttributes(ui::AXNodeData* node,
                                          AttributeUpdateType update_type) {
  CHECK(node);
  switch (update_type) {
    case AttributeUpdateType::kAdd:
      // Encode ARIA live region attributes including aria-atomic, aria-status,
      // and aria-relevant to define aria-live="polite" for this status node.
      node->AddBoolAttribute(ax::mojom::BoolAttribute::kLiveAtomic, true);
      node->AddStringAttribute(ax::mojom::StringAttribute::kLiveStatus,
                               "polite");
      node->AddStringAttribute(ax::mojom::StringAttribute::kLiveRelevant,
                               "additions text");
      // The status node is the root of live region. Use `kContainerLive*`
      // attributes to define this node as the root of the live region.
      node->AddBoolAttribute(ax::mojom::BoolAttribute::kContainerLiveAtomic,
                             true);
      node->AddStringAttribute(ax::mojom::StringAttribute::kContainerLiveStatus,
                               "polite");
      node->AddStringAttribute(
          ax::mojom::StringAttribute::kContainerLiveRelevant, "additions text");
      break;
    case AttributeUpdateType::kRemove:
      node->RemoveBoolAttribute(ax::mojom::BoolAttribute::kLiveAtomic);
      node->RemoveStringAttribute(ax::mojom::StringAttribute::kLiveStatus);
      node->RemoveStringAttribute(ax::mojom::StringAttribute::kLiveRelevant);
      node->RemoveBoolAttribute(ax::mojom::BoolAttribute::kContainerLiveAtomic);
      node->RemoveStringAttribute(
          ax::mojom::StringAttribute::kContainerLiveStatus);
      node->RemoveStringAttribute(
          ax::mojom::StringAttribute::kContainerLiveRelevant);
      break;
  }
}

std::unique_ptr<ui::AXNodeData> CreateStatusNodeStaticText(
    ui::AXNodeID id,
    ui::AXNodeData* parent_node) {
  // Creates a static text node for the status node to make it look like a
  // rendered text.
  std::unique_ptr<ui::AXNodeData> node = CreateNode(
      ax::mojom::Role::kStaticText, ax::mojom::Restriction::kReadOnly, id);
  node->relative_bounds = parent_node->relative_bounds;
  node->AddStringAttribute(ax::mojom::StringAttribute::kName, std::string());

  // The static text node will be added as the first node to its parent node as
  // the parent node will contain only this static text node.
  CHECK(parent_node->child_ids.empty());
  parent_node->child_ids.push_back(node->id);
  VLOG(2) << "Creating a static text for OCR status node.";
  return node;
}

std::unique_ptr<ui::AXNodeData> CreateStatusNode(ui::AXNodeID id,
                                                 ui::AXNodeData* parent_node,
                                                 bool currently_in_foreground) {
  // Create a status node that conveys a notification message and place the
  // message inside an appropriate ARIA landmark for easy navigation.
  std::unique_ptr<ui::AXNodeData> node = CreateNode(
      ax::mojom::Role::kStatus, ax::mojom::Restriction::kReadOnly, id);
  node->relative_bounds = parent_node->relative_bounds;
  if (currently_in_foreground) {
    UpdateStatusNodeLiveRegionAttributes(node.get(), AttributeUpdateType::kAdd);
  }

  // The status node will be added as the first node to its parent node as the
  // parent node will contain only this status node.
  CHECK(parent_node->child_ids.empty());
  parent_node->child_ids.push_back(node->id);
  VLOG(2) << "Creating an OCR status node.";
  return node;
}

// TODO(crbug.com/326131114): May need to give it a proper name or title.
// Revisit this banner node to understand why it is here besides navigation.
std::unique_ptr<ui::AXNodeData> CreateBannerNode(ui::AXNodeID id,
                                                 ui::AXNodeData* root_node) {
  // Create a banner node with an appropriate ARIA landmark for easy navigation.
  // This banner node will contain a status node later.
  std::unique_ptr<ui::AXNodeData> banner_node = CreateNode(
      ax::mojom::Role::kBanner, ax::mojom::Restriction::kReadOnly, id);
  // Set the origin of this node to be offscreen with an 1x1 rectangle as
  // both this wrapper and a status node don't have a visual element. The origin
  // of the doc is (0, 0), so setting (-1, -1) will make this node offscreen.
  banner_node->relative_bounds.bounds = gfx::RectF(-1, -1, 1, 1);
  banner_node->relative_bounds.offset_container_id = root_node->id;
  // As we create this status node's wrapper right after the PDF root node,
  // this node will be added as the first node to the PDF accessibility tree.
  CHECK(root_node->child_ids.empty());
  root_node->child_ids.push_back(banner_node->id);
  return banner_node;
}

}  // namespace

PdfAccessibilityTree::PdfAccessibilityTree(
    content::RenderFrame* render_frame,
    chrome_pdf::PdfAccessibilityActionHandler* action_handler,
    blink::WebPluginContainer* plugin_container)
    : content::RenderFrameObserver(render_frame),
      action_handler_(action_handler),
      plugin_container_(plugin_container) {
  DCHECK(render_frame);
  DCHECK(action_handler_);
  MaybeHandleAccessibilityChange(/*always_load_or_reload_accessibility=*/false);
}

PdfAccessibilityTree::~PdfAccessibilityTree() {
  UpdateDependentObjects(/*set_this=*/false);
}

// static
bool PdfAccessibilityTree::IsDataFromPluginValid(
    const std::vector<chrome_pdf::AccessibilityTextRunInfo>& text_runs,
    const std::vector<chrome_pdf::AccessibilityCharInfo>& chars,
    const chrome_pdf::AccessibilityPageObjects& page_objects) {
  base::CheckedNumeric<uint32_t> char_length = 0;
  for (const chrome_pdf::AccessibilityTextRunInfo& text_run : text_runs)
    char_length += text_run.len;

  if (!char_length.IsValid() || char_length.ValueOrDie() != chars.size())
    return false;

  const std::vector<chrome_pdf::AccessibilityLinkInfo>& links =
      page_objects.links;
  if (!std::is_sorted(
          links.begin(), links.end(),
          CompareTextRunsWithRange<chrome_pdf::AccessibilityLinkInfo>)) {
    return false;
  }
  // Text run index of a `link` is out of bounds if it exceeds the size of
  // `text_runs`. The index denotes the position of the link relative to the
  // text runs. The index value equal to the size of `text_runs` indicates that
  // the link should be after the last text run.
  // `index_in_page` of every `link` should be with in the range of total number
  // of links, which is size of `links`.
  for (const chrome_pdf::AccessibilityLinkInfo& link : links) {
    base::CheckedNumeric<size_t> index = link.text_range.index;
    index += link.text_range.count;
    if (!index.IsValid() || index.ValueOrDie() > text_runs.size() ||
        link.index_in_page >= links.size()) {
      return false;
    }
  }

  const std::vector<chrome_pdf::AccessibilityImageInfo>& images =
      page_objects.images;
  if (!std::is_sorted(images.begin(), images.end(),
                      CompareTextRuns<chrome_pdf::AccessibilityImageInfo>)) {
    return false;
  }
  // Text run index of an `image` works on the same logic as the text run index
  // of a `link` as described above.
  for (const chrome_pdf::AccessibilityImageInfo& image : images) {
    if (image.text_run_index > text_runs.size())
      return false;
  }

  const std::vector<chrome_pdf::AccessibilityHighlightInfo>& highlights =
      page_objects.highlights;
  if (!std::is_sorted(
          highlights.begin(), highlights.end(),
          CompareTextRunsWithRange<chrome_pdf::AccessibilityHighlightInfo>)) {
    return false;
  }

  // Since highlights also span across text runs similar to links, the
  // validation method is the same.
  // `index_in_page` of a `highlight` follows the same index validation rules
  // as of links.
  for (const auto& highlight : highlights) {
    base::CheckedNumeric<size_t> index = highlight.text_range.index;
    index += highlight.text_range.count;
    if (!index.IsValid() || index.ValueOrDie() > text_runs.size() ||
        highlight.index_in_page >= highlights.size()) {
      return false;
    }
  }

  const std::vector<chrome_pdf::AccessibilityTextFieldInfo>& text_fields =
      page_objects.form_fields.text_fields;
  if (!std::is_sorted(
          text_fields.begin(), text_fields.end(),
          CompareTextRuns<chrome_pdf::AccessibilityTextFieldInfo>)) {
    return false;
  }
  // Text run index of an `text_field` works on the same logic as the text run
  // index of a `link` as mentioned above.
  // `index_in_page` of a `text_field` follows the same index validation rules
  // as of links.
  for (const chrome_pdf::AccessibilityTextFieldInfo& text_field : text_fields) {
    if (text_field.text_run_index > text_runs.size() ||
        text_field.index_in_page >= text_fields.size()) {
      return false;
    }
  }

  const std::vector<chrome_pdf::AccessibilityChoiceFieldInfo>& choice_fields =
      page_objects.form_fields.choice_fields;
  if (!std::is_sorted(
          choice_fields.begin(), choice_fields.end(),
          CompareTextRuns<chrome_pdf::AccessibilityChoiceFieldInfo>)) {
    return false;
  }
  for (const auto& choice_field : choice_fields) {
    // Text run index of an `choice_field` works on the same logic as the text
    // run index of a `link` as mentioned above.
    // `index_in_page` of a `choice_field` follows the same index validation
    // rules as of links.
    if (choice_field.text_run_index > text_runs.size() ||
        choice_field.index_in_page >= choice_fields.size()) {
      return false;
    }

    // The type should be valid.
    if (choice_field.type < chrome_pdf::ChoiceFieldType::kMinValue ||
        choice_field.type > chrome_pdf::ChoiceFieldType::kMaxValue) {
      return false;
    }
  }

  const std::vector<chrome_pdf::AccessibilityButtonInfo>& buttons =
      page_objects.form_fields.buttons;
  if (!std::is_sorted(buttons.begin(), buttons.end(),
                      CompareTextRuns<chrome_pdf::AccessibilityButtonInfo>)) {
    return false;
  }
  for (const chrome_pdf::AccessibilityButtonInfo& button : buttons) {
    // Text run index of an `button` works on the same logic as the text run
    // index of a `link` as mentioned above.
    // `index_in_page` of a `button` follows the same index validation rules as
    // of links.
    if (button.text_run_index > text_runs.size() ||
        button.index_in_page >= buttons.size()) {
      return false;
    }

    // The type should be valid.
    if (button.type < chrome_pdf::ButtonType::kMinValue ||
        button.type > chrome_pdf::ButtonType::kMaxValue) {
      return false;
    }

    // For radio button or checkbox, value of `button.control_index` should
    // always be less than `button.control_count`.
    if ((button.type == chrome_pdf::ButtonType::kCheckBox ||
         button.type == chrome_pdf::ButtonType::kRadioButton) &&
        (button.control_index >= button.control_count)) {
      return false;
    }
  }

  return true;
}

void PdfAccessibilityTree::SetAccessibilityViewportInfo(
    chrome_pdf::AccessibilityViewportInfo viewport_info) {
  // This call may trigger layout, and ultimately self-deletion; see
  // crbug.com/1274376 for details.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&PdfAccessibilityTree::DoSetAccessibilityViewportInfo,
                     GetWeakPtr(), std::move(viewport_info)));
}

void PdfAccessibilityTree::DoSetAccessibilityViewportInfo(
    const chrome_pdf::AccessibilityViewportInfo& viewport_info) {
  zoom_ = viewport_info.zoom;
  scale_ = viewport_info.scale;
  CHECK_GT(zoom_, 0);
  CHECK_GT(scale_, 0);
  scroll_ = gfx::PointF(viewport_info.scroll).OffsetFromOrigin();
  offset_ = gfx::PointF(viewport_info.offset).OffsetFromOrigin();
  orientation_ = viewport_info.orientation;
  selection_ = viewport_info.selection;

  auto obj = GetPluginContainerAXObject();
  if (obj && tree_.size() > 1) {
    ui::AXNode* root = tree_.root();
    ui::AXNodeData root_data = root->data();
    root_data.relative_bounds.transform = MakeTransformFromViewInfo();
    root->SetData(root_data);
    UpdateAXTreeDataFromSelection();
    MarkPluginContainerDirty();
  }
}

void PdfAccessibilityTree::SetAccessibilityDocInfo(
    std::unique_ptr<chrome_pdf::AccessibilityDocInfo> doc_info) {
  CHECK(doc_info);
  // This call may trigger layout, and ultimately self-deletion; see
  // crbug.com/1274376 for details.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&PdfAccessibilityTree::DoSetAccessibilityDocInfo,
                     GetWeakPtr(), std::move(doc_info)));
}

void PdfAccessibilityTree::DoSetAccessibilityDocInfo(
    std::unique_ptr<chrome_pdf::AccessibilityDocInfo> doc_info) {
  CHECK(doc_info);
  auto obj = GetPluginContainerAXObject();
  if (!obj) {
    return;
  }

  ClearAccessibilityNodes();
  page_count_ = doc_info->page_count;
  is_tagged_ = doc_info->is_tagged;

  doc_node_ =
      CreateNode(ax::mojom::Role::kPdfRoot, ax::mojom::Restriction::kReadOnly,
                 obj->GenerateAXID());
  doc_node_->AddState(ax::mojom::State::kFocusable);
  doc_node_->AddStringAttribute(ax::mojom::StringAttribute::kName,
                                l10n_util::GetPluralStringFUTF8(
                                    IDS_PDF_DOCUMENT_PAGE_COUNT, page_count_));

  // Because all of the coordinates are expressed relative to the
  // doc's coordinates, the origin of the doc must be (0, 0). Its
  // width and height will be updated as we add each page so that the
  // doc's bounding box surrounds all pages.
  doc_node_->relative_bounds.bounds = gfx::RectF(0, 0, 1, 1);

  // This notification node needs to be added as the first node in the PDF
  // accessibility tree so that the user will reach out to this node first when
  // navigating the PDF accessibility tree.
  banner_node_ = CreateBannerNode(obj->GenerateAXID(), doc_node_.get());
  status_node_ = CreateStatusNode(obj->GenerateAXID(), banner_node_.get(),
                                  currently_in_foreground_);
  status_node_text_ =
      CreateStatusNodeStaticText(obj->GenerateAXID(), status_node_.get());

  SetStatusMessage(IDS_PDF_LOADING_TO_A11Y_TREE);

  // Create a PDF accessibility tree with the status node first to notify users
  // that PDF content is being loaded.
  ui::AXTreeUpdate update;
  // We need to set the `AXTreeID` both in the `AXTreeUpdate` and the
  // `AXTreeData` member because the constructor of `AXTree` might expect the
  // tree to be constructed with a valid tree ID.
  update.has_tree_data = true;
  const auto& tree_id = render_frame()->GetWebFrame()->GetAXTreeID();
  update.tree_data.tree_id = tree_id;
  tree_data_.tree_id = tree_id;
  tree_data_.focus_id = doc_node_->id;
  update.root_id = doc_node_->id;
  update.nodes = {*doc_node_, *banner_node_, *status_node_, *status_node_text_};
  if (!tree_.Unserialize(update)) {
    LOG(FATAL) << tree_.error();
  }

  MarkPluginContainerDirty();
}

void PdfAccessibilityTree::SetAccessibilityPageInfo(
    chrome_pdf::AccessibilityPageInfo page_info,
    std::vector<chrome_pdf::AccessibilityTextRunInfo> text_runs,
    std::vector<chrome_pdf::AccessibilityCharInfo> chars,
    chrome_pdf::AccessibilityPageObjects page_objects) {
  // This call may trigger layout, and ultimately self-deletion; see
  // crbug.com/1274376 for details.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&PdfAccessibilityTree::DoSetAccessibilityPageInfo,
                     GetWeakPtr(), std::move(page_info), std::move(text_runs),
                     std::move(chars), std::move(page_objects)));
}

#if BUILDFLAG(ENABLE_SCREEN_AI_SERVICE)
void PdfAccessibilityTree::OnHasSearchifyText() {
  if (!render_frame()) {
    return;
  }
  content::RenderAccessibility* render_accessibility =
      render_frame()->GetRenderAccessibility();
  bool screen_reader_mode =
      (render_accessibility && render_accessibility->GetAXMode().has_mode(
                                   ui::AXMode::kExtendedProperties));
  base::UmaHistogramBoolean(
      "Accessibility.ScreenAI.Searchify.ScreenReaderModeEnabled",
      screen_reader_mode);
}
#endif  // BUILDFLAG(ENABLE_SCREEN_AI_SERVICE)

void PdfAccessibilityTree::DoSetAccessibilityPageInfo(
    const chrome_pdf::AccessibilityPageInfo& page_info,
    const std::vector<chrome_pdf::AccessibilityTextRunInfo>& text_runs,
    const std::vector<chrome_pdf::AccessibilityCharInfo>& chars,
    const chrome_pdf::AccessibilityPageObjects& page_objects) {
  // Outdated calls are ignored.
  uint32_t page_index = page_info.page_index;
  if (page_index != next_page_index_)
    return;

  auto obj = GetPluginContainerAXObject();
  if (!obj) {
    return;
  }

#if BUILDFLAG(ENABLE_SCREEN_AI_SERVICE)
  bool first_searchified_page = page_info.is_searchified && !did_searchify_run_;
  did_searchify_run_ |= page_info.is_searchified;
  if (!was_text_converted_from_image_ && page_info.is_searchified) {
    // `page_info.is_searchified` is true when Searchify is run on the page, but
    // if it did not find any text, `is_searchified` will be false for all
    // `text_run`s.
    for (const auto& text_run : text_runs) {
      if (text_run.is_searchified) {
        was_text_converted_from_image_ = true;
        break;
      }
    }
  }
#endif  // BUILDFLAG(ENABLE_SCREEN_AI_SERVICE)

  // If unsanitized data is found, don't trust it and stop creation of the
  // accessibility tree. Now that we already created the initial tree with the
  // root node and the status node, destroy the existing tree as well.
  if (!invalid_plugin_message_received_) {
    invalid_plugin_message_received_ =
        !IsDataFromPluginValid(text_runs, chars, page_objects);
  }
  if (invalid_plugin_message_received_) {
    if (tree_.root()) {
      tree_.Destroy();
      banner_node_.reset();
      status_node_.reset();
      status_node_text_.reset();
    }
    return;
  }

  CHECK_LT(page_index, page_count_);
  ++next_page_index_;
  // Update `had_accessible_text_` before calling `AddPageContent()` as this
  // variable will be used inside of `AddPageContent()`. If the page is
  // searchified, it indicates that the page was not originally accessible.
#if BUILDFLAG(ENABLE_SCREEN_AI_SERVICE)
  had_accessible_text_ |= (!page_info.is_searchified && !text_runs.empty());
#else
  had_accessible_text_ |= !text_runs.empty();
#endif  // BUILDFLAG(ENABLE_SCREEN_AI_SERVICE)

  AddPageContent(page_info, page_index, text_runs, chars, page_objects);

  bool has_image = !page_objects.images.empty();
  did_have_an_image_ |= has_image;

  if (page_index == page_count_ - 1) {
    SetFinalStatusMessage();
    if (!had_accessible_text_) {
      base::UmaHistogramCounts1000(
          "Accessibility.PdfOcr.InaccessiblePdfPageCount", page_count_);
    }
  } else {
#if BUILDFLAG(ENABLE_SCREEN_AI_SERVICE)
    // If this is the first page with searchify results, notify the user that
    // OCR is in progress.
    if (first_searchified_page) {
      SetStatusMessage(IDS_PDF_OCR_IN_PROGRESS);
    }
#endif
  }

  UnserializeNodes();
}

void PdfAccessibilityTree::SetFinalStatusMessage() {
#if BUILDFLAG(ENABLE_SCREEN_AI_SERVICE)
  if (did_searchify_run_) {
    SetStatusMessage(was_text_converted_from_image_ ? IDS_PDF_OCR_COMPLETED
                                                    : IDS_PDF_OCR_NO_RESULT);
    return;
  }
  // Show promotion if the PDF had images and searchify did not run. Promotion
  // is not needed when searchify run but did not find any results.
  if (!did_searchify_run_ && did_have_an_image_) {
    SetStatusMessage(IDS_PDF_OCR_FEATURE_ALERT);
    return;
  }
#endif  // BUILDFLAG(ENABLE_SCREEN_AI_SERVICE)
  if (had_accessible_text_ || !did_have_an_image_) {
    // Set the status node to notify users that the PDF content has been loaded
    // into an accessibility tree.
    SetStatusMessage(IDS_PDF_LOADED_TO_A11Y_TREE);

    // Reset the status node's attributes after a delay. This delay allows
    // screen reader to deliver the user the notification message set above.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&PdfAccessibilityTree::ResetStatusNodeAttributes,
                       GetWeakPtr()),
        kDelayBeforeResettingStatusNode);
  }
}

void PdfAccessibilityTree::AddPageContent(
    const chrome_pdf::AccessibilityPageInfo& page_info,
    uint32_t page_index,
    const std::vector<chrome_pdf::AccessibilityTextRunInfo>& text_runs,
    const std::vector<chrome_pdf::AccessibilityCharInfo>& chars,
    const chrome_pdf::AccessibilityPageObjects& page_objects) {
  CHECK(doc_node_);
  auto obj = GetPluginContainerAXObject();
  CHECK(obj);
  PdfAccessibilityTreeBuilder tree_builder(
      /*mark_headings_using_heuristic=*/!is_tagged_, text_runs, chars,
      page_objects, page_info, page_index, doc_node_.get(), &(*obj), &nodes_,
      &node_id_to_page_char_index_, &node_id_to_annotation_info_
  );
  tree_builder.BuildPageTree();
}

void PdfAccessibilityTree::UnserializeNodes() {
  auto obj = GetPluginContainerAXObject();
  if (!obj) {
    return;
  }

  doc_node_->relative_bounds.transform = MakeTransformFromViewInfo();

  ui::AXTreeUpdate update;
  update.root_id = doc_node_->id;
  update.nodes.push_back(*doc_node_);
  update.nodes.push_back(*status_node_);
  update.nodes.push_back(*status_node_text_);
  for (const auto& node : nodes_) {
    obj->MarkPluginDescendantDirty(node->id);
    update.nodes.push_back(std::move(*node));
  }

  if (!tree_.Unserialize(update))
    LOG(FATAL) << tree_.error();

  UpdateAXTreeDataFromSelection();

  MarkPluginContainerDirty();

  nodes_.clear();
}

void PdfAccessibilityTree::SetStatusMessage(int message_id) {
  CHECK(status_node_);
  CHECK(status_node_text_);
  const std::string message = l10n_util::GetStringUTF8(message_id);
  VLOG(2) << "Setting the status node with message: " << message;
  status_node_->SetNameChecked(message);
  status_node_text_->SetNameChecked(message);

  auto obj = GetPluginContainerAXObject();
  CHECK(obj);
  obj->MarkPluginDescendantDirty(banner_node_->id);
}

void PdfAccessibilityTree::ResetStatusNodeAttributes() {
  auto obj = GetPluginContainerAXObject();
  if (!obj) {
    return;
  }

  CHECK(status_node_);
  CHECK(status_node_text_);
  // Clear out its live region and name attributes as it is no longer necessary
  // to keep the status node in this case. The node may not have live region
  // attributes. However, it is okay to try removing them from the node as
  // removing will be performed only when the node has those attributes.
  UpdateStatusNodeLiveRegionAttributes(status_node_.get(),
                                       AttributeUpdateType::kRemove);
  status_node_->RemoveStringAttribute(ax::mojom::StringAttribute::kName);
  status_node_text_->RemoveStringAttribute(ax::mojom::StringAttribute::kName);

  ui::AXTreeUpdate update;
  update.root_id = doc_node_->id;
  // `status_node_` has been either cleared out or set with a new message, so
  // add it to `ui::AXTreeUpdate`.
  update.nodes.push_back(*status_node_);
  update.nodes.push_back(*status_node_text_);
  if (!tree_.Unserialize(update)) {
    LOG(FATAL) << tree_.error();
  }
  MarkPluginContainerDirty();
}

void PdfAccessibilityTree::UpdateAXTreeDataFromSelection() {
  // The tree should contain a node for each page and one additional node, a
  // status node (see UnserializeNodes()). If the tree is not yet fully
  // populated with these nodes, a selection should not be possible.
  if (page_count_ != tree_.root()->children().size() - 1) {
    return;
  }

  tree_data_.sel_is_backward = false;
  if (selection_.start.page_index > selection_.end.page_index) {
    tree_data_.sel_is_backward = true;
  } else if (selection_.start.page_index == selection_.end.page_index &&
             selection_.start.char_index > selection_.end.char_index) {
    tree_data_.sel_is_backward = true;
  }

  FindNodeOffset(selection_.start.page_index, selection_.start.char_index,
                 &tree_data_.sel_anchor_object_id,
                 &tree_data_.sel_anchor_offset);
  FindNodeOffset(selection_.end.page_index, selection_.end.char_index,
                 &tree_data_.sel_focus_object_id, &tree_data_.sel_focus_offset);
}

void PdfAccessibilityTree::FindNodeOffset(uint32_t page_index,
                                          uint32_t page_char_index,
                                          int32_t* out_node_id,
                                          int32_t* out_node_char_index) const {
  *out_node_id = -1;
  *out_node_char_index = 0;
  ui::AXNode* root = tree_.root();
  // Use page_index + 1 since the first node in the tree is the status node, not
  // an actual page node.
  if (page_index + 1 >= root->children().size()) {
    return;
  }
  ui::AXNode* page = root->children()[page_index + 1];

  // Iterate over all paragraphs within this given page, and static text nodes
  // within each paragraph.
  for (ui::AXNode* para : page->children()) {
    for (ui::AXNode* child_node : para->children()) {
      ui::AXNode* static_text = GetStaticTextNodeFromNode(child_node);
      if (!static_text)
        continue;
      // Look up the page-relative character index for static nodes from a map
      // we built while the document was initially built.
      auto iter = node_id_to_page_char_index_.find(static_text->id());
      uint32_t char_index = iter->second.char_index;
      uint32_t len = static_text->data()
                         .GetStringAttribute(ax::mojom::StringAttribute::kName)
                         .size();

      // If the character index we're looking for falls within the range
      // of this node, return the node ID and index within this node's text.
      if (page_char_index <= char_index + len) {
        *out_node_id = static_text->id();
        *out_node_char_index = page_char_index - char_index;
        return;
      }
    }
  }
}

bool PdfAccessibilityTree::FindCharacterOffset(
    const ui::AXNode& node,
    uint32_t char_offset_in_node,
    chrome_pdf::PageCharacterIndex& page_char_index) const {
  auto iter = node_id_to_page_char_index_.find(GetId(&node));
  if (iter == node_id_to_page_char_index_.end())
    return false;
  page_char_index.char_index = iter->second.char_index + char_offset_in_node;
  page_char_index.page_index = iter->second.page_index;
  return true;
}

void PdfAccessibilityTree::ClearAccessibilityNodes() {
  next_page_index_ = 0;
  doc_node_.reset();
  status_node_.reset();
  banner_node_.reset();
  nodes_.clear();
  node_id_to_page_char_index_.clear();
  node_id_to_annotation_info_.clear();
}

std::optional<blink::WebAXObject>
PdfAccessibilityTree::GetPluginContainerAXObject() {
  // Might be nullptr within tests.
  if (!plugin_container_) {
    CHECK_IS_TEST();
    if (force_plugin_ax_object_for_testing_.IsDetached()) {
      return std::nullopt;
    }
    return blink::WebAXObject(force_plugin_ax_object_for_testing_);
  }

  const blink::WebAXObject& obj =
      blink::WebAXObject::FromWebNode(plugin_container_->GetElement());
  if (obj.IsDetached()) {
    return std::nullopt;
  }
  return obj;
}

std::unique_ptr<gfx::Transform>
PdfAccessibilityTree::MakeTransformFromViewInfo() const {
  double applicable_scale_factor = scale_;
  auto transform = std::make_unique<gfx::Transform>();
  // `scroll_` represents the offset from which PDF content starts. It is the
  // height of the PDF toolbar and the width of sidenav in pixels if it is open.
  // Sizes of PDF toolbar and sidenav do not change with zoom.
  transform->Scale(applicable_scale_factor, applicable_scale_factor);
  transform->Translate(-scroll_);
  transform->Scale(zoom_, zoom_);
  transform->Translate(offset_);
  return transform;
}

PdfAccessibilityTree::AnnotationInfo::AnnotationInfo(uint32_t page_index,
                                                     uint32_t annotation_index)
    : page_index(page_index), annotation_index(annotation_index) {}

PdfAccessibilityTree::AnnotationInfo::AnnotationInfo(
    const AnnotationInfo& other) = default;

PdfAccessibilityTree::AnnotationInfo::~AnnotationInfo() = default;

//
// AXTreeSource implementation.
//

bool PdfAccessibilityTree::GetTreeData(ui::AXTreeData* tree_data) const {
  // This tree may not yet be fully constructed.
  if (!tree_.root()) {
    return false;
  }

  tree_data->tree_id = render_frame()->GetWebFrame()->GetAXTreeID();
  tree_data->focus_id = tree_data_.focus_id;
  tree_data->sel_is_backward = tree_data_.sel_is_backward;
  tree_data->sel_anchor_object_id = tree_data_.sel_anchor_object_id;
  tree_data->sel_anchor_offset = tree_data_.sel_anchor_offset;
  tree_data->sel_focus_object_id = tree_data_.sel_focus_object_id;
  tree_data->sel_focus_offset = tree_data_.sel_focus_offset;
  return true;
}

ui::AXNode* PdfAccessibilityTree::GetRoot() const {
  return tree_.root();
}

ui::AXNode* PdfAccessibilityTree::GetFromId(int32_t id) const {
  return tree_.GetFromId(id);
}

int32_t PdfAccessibilityTree::GetId(const ui::AXNode* node) const {
  return node->id();
}

size_t PdfAccessibilityTree::GetChildCount(const ui::AXNode* node) const {
  return node->children().size();
}

const ui::AXNode* PdfAccessibilityTree::ChildAt(const ui::AXNode* node,
                                                size_t index) const {
  return node->children()[index];
}

ui::AXNode* PdfAccessibilityTree::GetParent(const ui::AXNode* node) const {
  return node->parent();
}

bool PdfAccessibilityTree::IsIgnored(const ui::AXNode* node) const {
  return node->IsIgnored();
}

bool PdfAccessibilityTree::IsEqual(const ui::AXNode* node1,
                                   const ui::AXNode* node2) const {
  return node1 == node2;
}

const ui::AXNode* PdfAccessibilityTree::GetNull() const {
  return nullptr;
}

void PdfAccessibilityTree::SerializeNode(const ui::AXNode* node,
                                         ui::AXNodeData* out_data) const {
  *out_data = node->data();
}

std::unique_ptr<ui::AXActionTarget> PdfAccessibilityTree::CreateActionTarget(
    ui::AXNodeID id) {
  ui::AXNode* target_node = GetFromId(id);
  if (!target_node) {
    return std::make_unique<ui::NullAXActionTarget>();
  }
  return std::make_unique<PdfAXActionTarget>(*target_node, this);
}

void PdfAccessibilityTree::AccessibilityModeChanged(const ui::AXMode& mode) {
  if (mode.is_mode_off()) {
    UpdateDependentObjects(/*set_this=*/false);
    return;
  }

  MaybeHandleAccessibilityChange(
      /*always_load_or_reload_accessibility=*/true);
}

void PdfAccessibilityTree::WasHidden() {
  currently_in_foreground_ = false;
}

void PdfAccessibilityTree::WasShown() {
  currently_in_foreground_ = true;
}

bool PdfAccessibilityTree::ShowContextMenu() {
  auto obj = GetPluginContainerAXObject();
  if (!obj) {
    return false;
  }

  ui::AXActionData action_data;
  action_data.action = ax::mojom::Action::kShowContextMenu;
  return obj->PerformAction(action_data);
}

bool PdfAccessibilityTree::SetChildTree(const ui::AXNodeID& target_node_id,
                                        const ui::AXTreeID& child_tree_id) {
  ui::AXNode* target_node = tree_.GetFromId(target_node_id);
  if (!target_node) {
    return false;
  }
  // `nodes_` will be empty once they are unserialized to `tree_`.
  if (!nodes_.empty()) {
    // The `tree_` is not yet fully loaded, thus unable to stitch.
    return false;
  }
  auto obj = GetPluginContainerAXObject();
  if (!obj) {
    return false;
  }
  ui::AXTreeUpdate tree_update;
  ui::AXNodeData target_node_data = target_node->data();
  target_node_data.child_ids = {};
  target_node_data.AddChildTreeId(child_tree_id);
  tree_update.root_id = doc_node_->id;
  tree_update.nodes = {target_node_data};
  CHECK(tree_.Unserialize(tree_update)) << tree_.error();
  MarkPluginContainerDirty();
  return true;
}

void PdfAccessibilityTree::HandleAction(
    const chrome_pdf::AccessibilityActionData& action_data) {
  action_handler_->HandleAccessibilityAction(action_data);
}

std::optional<PdfAccessibilityTree::AnnotationInfo>
PdfAccessibilityTree::GetPdfAnnotationInfoFromAXNode(int32_t ax_node_id) const {
  auto iter = node_id_to_annotation_info_.find(ax_node_id);
  if (iter == node_id_to_annotation_info_.end())
    return std::nullopt;

  return AnnotationInfo(iter->second.page_index, iter->second.annotation_index);
}

void PdfAccessibilityTree::MaybeHandleAccessibilityChange(
    bool always_load_or_reload_accessibility) {
  // This call ensures Blink accessibility always knows about us after it gets
  // created for any reason e.g. mode changes, startup, etc.
  if (UpdateDependentObjects(/*set_this=*/true)) {
    if (always_load_or_reload_accessibility) {
      action_handler_->LoadOrReloadAccessibility();
    } else {
      action_handler_->EnableAccessibility();
    }
  }
}

void PdfAccessibilityTree::MarkPluginContainerDirty() {
  // Might be nullptr within tests.
  if (!plugin_container_) {
    CHECK_IS_TEST();
    return;
  }

  const blink::WebAXObject& obj =
      blink::WebAXObject::FromWebNode(plugin_container_->GetElement());
  if (obj.IsDetached()) {
    return;
  }

  obj.AddDirtyObjectToSerializationQueue(ax::mojom::EventFrom::kNone,
                                         ax::mojom::Action::kNone,
                                         std::vector<ui::AXEventIntent>());
}

bool PdfAccessibilityTree::UpdateDependentObjects(bool set_this) {
  bool success = true;

  // TODO(accessibility): remove this dependency.
  content::RenderAccessibility* render_accessibility =
      render_frame() ? render_frame()->GetRenderAccessibility() : nullptr;
  if (render_accessibility) {
    render_accessibility->SetPluginAXTreeActionTargetAdapter(
        set_this ? this : nullptr);
  } else {
    success = false;
  }

  auto obj = GetPluginContainerAXObject();
  if (obj && !obj->IsDetached()) {
    obj->SetPluginTreeSource(set_this ? this : nullptr);
  } else {
    success &= !force_plugin_ax_object_for_testing_.IsDetached();
  }

  return success;
}

void PdfAccessibilityTree::ForcePluginAXObjectForTesting(
    const blink::WebAXObject& obj) {
  CHECK_IS_TEST();
  force_plugin_ax_object_for_testing_ = obj;
  UpdateDependentObjects(/*set_this=*/true);
}

}  // namespace pdf
