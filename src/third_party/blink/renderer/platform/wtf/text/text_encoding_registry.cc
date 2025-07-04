/*
 * Copyright (C) 2006, 2007, 2011 Apple Inc. All rights reserved.
 * Copyright (C) 2007-2009 Torch Mobile, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "third_party/blink/renderer/platform/wtf/text/text_encoding_registry.h"

#include <atomic>
#include <memory>

#include "base/compiler_specific.h"
#include "base/dcheck_is_on.h"
#include "base/feature_list.h"
#include "base/logging.h"
#include "base/synchronization/lock.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/renderer/platform/wtf/hash_map.h"
#include "third_party/blink/renderer/platform/wtf/hash_set.h"
#include "third_party/blink/renderer/platform/wtf/std_lib_extras.h"
#include "third_party/blink/renderer/platform/wtf/text/ascii_ctype.h"
#include "third_party/blink/renderer/platform/wtf/text/atomic_string.h"
#include "third_party/blink/renderer/platform/wtf/text/case_folding_hash.h"
#include "third_party/blink/renderer/platform/wtf/text/character_visitor.h"
#include "third_party/blink/renderer/platform/wtf/text/string_view.h"
#include "third_party/blink/renderer/platform/wtf/text/text_codec_cjk.h"
#include "third_party/blink/renderer/platform/wtf/text/text_codec_icu.h"
#include "third_party/blink/renderer/platform/wtf/text/text_codec_latin1.h"
#include "third_party/blink/renderer/platform/wtf/text/text_codec_replacement.h"
#include "third_party/blink/renderer/platform/wtf/text/text_codec_user_defined.h"
#include "third_party/blink/renderer/platform/wtf/text/text_codec_utf16.h"
#include "third_party/blink/renderer/platform/wtf/text/text_codec_utf8.h"
#include "third_party/blink/renderer/platform/wtf/text/text_encoding.h"
#include "third_party/blink/renderer/platform/wtf/wtf.h"

namespace blink {

const size_t kMaxEncodingNameLength = 63;

struct TextCodecFactory {
  NewTextCodecFunction function;
  explicit TextCodecFactory(NewTextCodecFunction f = nullptr) : function(f) {}
};

typedef HashMap<const char*, const char*, CaseFoldingHashTraits<const char*>>
    TextEncodingNameMap;
typedef HashMap<String, TextCodecFactory> TextCodecMap;

static base::Lock& EncodingRegistryLock() {
  DEFINE_THREAD_SAFE_STATIC_LOCAL(base::Lock, lock, ());
  return lock;
}

static TextEncodingNameMap* g_text_encoding_name_map;
static TextCodecMap* g_text_codec_map;

namespace {
static std::atomic_bool g_did_extend_text_codec_maps{false};

ALWAYS_INLINE bool AtomicDidExtendTextCodecMaps() {
  return g_did_extend_text_codec_maps.load(std::memory_order_acquire);
}

ALWAYS_INLINE void AtomicSetDidExtendTextCodecMaps() {
  g_did_extend_text_codec_maps.store(true, std::memory_order_release);
}
}  // namespace

#if !DCHECK_IS_ON()

static inline void CheckExistingName(const char*, const char*) {}

#else

static void CheckExistingName(const char* alias, const char* atomic_name) {
  EncodingRegistryLock().AssertAcquired();
  const auto it = g_text_encoding_name_map->find(alias);
  if (it == g_text_encoding_name_map->end())
    return;
  const char* old_atomic_name = it->value;
  if (old_atomic_name == atomic_name)
    return;
  // Keep the warning silent about one case where we know this will happen.
  if (UNSAFE_TODO(strcmp(alias, "ISO-8859-8-I")) == 0 &&
      UNSAFE_TODO(strcmp(old_atomic_name, "ISO-8859-8-I")) == 0 &&
      EqualIgnoringASCIICase(atomic_name, "iso-8859-8")) {
    return;
  }
  LOG(ERROR) << "alias " << alias << " maps to " << old_atomic_name
             << " already, but someone is trying to make it map to "
             << atomic_name;
}

#endif

static bool IsUndesiredAlias(const char* alias) {
  // Reject aliases with version numbers that are supported by some back-ends
  // (such as "ISO_2022,locale=ja,version=0" in ICU).
  if (UNSAFE_TODO(strchr(alias, ','))) {
    return true;
  }
  // 8859_1 is known to (at least) ICU, but other browsers don't support this
  // name - and having it caused a compatibility
  // problem, see bug 43554.
  if (0 == UNSAFE_TODO(strcmp(alias, "8859_1"))) {
    return true;
  }
  return false;
}

static void AddToTextEncodingNameMap(const char* alias, const char* name) {
  DCHECK_LE(strlen(alias), kMaxEncodingNameLength);
  EncodingRegistryLock().AssertAcquired();
  if (IsUndesiredAlias(alias))
    return;
  const auto it = g_text_encoding_name_map->find(name);
  DCHECK(UNSAFE_TODO(strcmp(alias, name)) == 0 ||
         it != g_text_encoding_name_map->end());
  const char* atomic_name =
      it != g_text_encoding_name_map->end() ? it->value : name;
  CheckExistingName(alias, atomic_name);
  g_text_encoding_name_map->insert(alias, atomic_name);
}

static void AddToTextCodecMap(const char* name, NewTextCodecFunction function) {
  EncodingRegistryLock().AssertAcquired();
  g_text_codec_map->insert(AtomicString(name), TextCodecFactory(function));
}

// Note that this can be called both the main thread and worker threads.
static void BuildBaseTextCodecMaps() {
  DCHECK(!g_text_codec_map);
  DCHECK(!g_text_encoding_name_map);
  EncodingRegistryLock().AssertAcquired();

  g_text_codec_map = new TextCodecMap;
  g_text_encoding_name_map = new TextEncodingNameMap;

  TextCodecLatin1::RegisterEncodingNames(AddToTextEncodingNameMap);
  TextCodecLatin1::RegisterCodecs(AddToTextCodecMap);

  TextCodecUtf8::RegisterEncodingNames(AddToTextEncodingNameMap);
  TextCodecUtf8::RegisterCodecs(AddToTextCodecMap);

  TextCodecUtf16::RegisterEncodingNames(AddToTextEncodingNameMap);
  TextCodecUtf16::RegisterCodecs(AddToTextCodecMap);

  TextCodecUserDefined::RegisterEncodingNames(AddToTextEncodingNameMap);
  TextCodecUserDefined::RegisterCodecs(AddToTextCodecMap);
}

static void ExtendTextCodecMaps() {
  TextCodecReplacement::RegisterEncodingNames(AddToTextEncodingNameMap);
  TextCodecReplacement::RegisterCodecs(AddToTextCodecMap);

  TextCodecCjk::RegisterEncodingNames(AddToTextEncodingNameMap);
  TextCodecCjk::RegisterCodecs(AddToTextCodecMap);

  TextCodecIcu::RegisterEncodingNames(AddToTextEncodingNameMap);
  TextCodecIcu::RegisterCodecs(AddToTextCodecMap);
}

std::unique_ptr<TextCodec> NewTextCodec(const TextEncoding& encoding) {
  base::AutoLock lock(EncodingRegistryLock());

  DCHECK(g_text_codec_map);
  TextCodecFactory factory = g_text_codec_map->at(encoding.GetName());
  DCHECK(factory.function);
  return factory.function(encoding);
}

const char* AtomicCanonicalTextEncodingName(const char* name) {
  if (!name || !name[0])
    return nullptr;
  base::AutoLock lock(EncodingRegistryLock());

  if (!g_text_encoding_name_map)
    BuildBaseTextCodecMaps();

  const auto it1 = g_text_encoding_name_map->find(name);
  if (it1 != g_text_encoding_name_map->end())
    return it1->value;

  if (AtomicDidExtendTextCodecMaps())
    return nullptr;

  ExtendTextCodecMaps();
  AtomicSetDidExtendTextCodecMaps();
  const auto it2 = g_text_encoding_name_map->find(name);
  return it2 != g_text_encoding_name_map->end() ? it2->value : nullptr;
}

template <typename CharacterType>
const char* AtomicCanonicalTextEncodingName(
    base::span<const CharacterType> characters) {
  if (characters.size() > kMaxEncodingNameLength) {
    return nullptr;
  }
  std::array<char, kMaxEncodingNameLength + 1> buffer;
  size_t j = 0;
  for (size_t i = 0; i < characters.size(); ++i) {
    buffer[j++] = static_cast<char>(characters[i]);
  }
  buffer[j] = 0;
  return AtomicCanonicalTextEncodingName(buffer.data());
}

const char* AtomicCanonicalTextEncodingName(const String& alias) {
  if (alias.empty()) {
    return nullptr;
  }
  if (alias.Contains('\0')) {
    return nullptr;
  }
  if (!alias.ContainsOnlyASCIIOrEmpty()) {
    return nullptr;
  }
  return VisitCharacters(
      alias, [](auto chars) { return AtomicCanonicalTextEncodingName(chars); });
}

bool NoExtendedTextEncodingNameUsed() {
  return !AtomicDidExtendTextCodecMaps();
}

Vector<String> TextEncodingAliasesForTesting() {
  base::AutoLock lock(EncodingRegistryLock());
  if (!g_text_encoding_name_map) {
    BuildBaseTextCodecMaps();
  }
  if (!AtomicDidExtendTextCodecMaps()) {
    ExtendTextCodecMaps();
    AtomicSetDidExtendTextCodecMaps();
  }
  return Vector<String>(g_text_encoding_name_map->Keys());
}

#ifndef NDEBUG
void DumpTextEncodingNameMap() {
  unsigned size = g_text_encoding_name_map->size();
  fprintf(stderr, "Dumping %u entries in blink::TextEncodingNameMap...\n",
          size);

  base::AutoLock lock(EncodingRegistryLock());

  for (const auto& it : *g_text_encoding_name_map)
    UNSAFE_TODO(fprintf(stderr, "'%s' => '%s'\n", it.key, it.value));
}
#endif

}  // namespace blink
