/*
 * Copyright (C) 2011 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_LOADER_LINK_LOADER_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_LOADER_LINK_LOADER_H_

#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/loader/link_load_parameters.h"
#include "third_party/blink/renderer/platform/loader/fetch/fetch_parameters.h"

namespace blink {

class Document;
class LinkLoaderClient;
class PendingLinkPreload;
class ModuleScript;
class PrerenderHandle;
class Resource;
class ResourceClient;

// The LinkLoader can load link rel types icon, dns-prefetch, prefetch, and
// prerender.
class CORE_EXPORT LinkLoader final : public GarbageCollected<LinkLoader> {
 public:
  explicit LinkLoader(LinkLoaderClient*);

  void Abort();
  bool LoadLink(const LinkLoadParameters&, Document&);
  void LoadStylesheet(const LinkLoadParameters&,
                      const AtomicString&,
                      const TextEncoding&,
                      FetchParameters::DeferOption,
                      Document&,
                      ResourceClient*,
                      RenderBlockingBehavior);

  Resource* GetResourceForTesting();

  void NotifyModuleLoadFinished(ModuleScript*);
  void NotifyFinished(Resource*);

  void Trace(Visitor*) const;

 private:
  Member<LinkLoaderClient> client_;
  Member<PendingLinkPreload> pending_preload_;

  Member<PrerenderHandle> prerender_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_LOADER_LINK_LOADER_H_
