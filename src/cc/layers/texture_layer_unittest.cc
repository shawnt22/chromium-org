// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/390223051): Remove C-library calls to fix the errors.
#pragma allow_unsafe_libc_calls
#endif

#include "cc/layers/texture_layer.h"

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/location.h"
#include "base/memory/ptr_util.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/synchronization/lock.h"
#include "base/synchronization/waitable_event.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/single_thread_task_runner.h"
#include "base/threading/thread.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "cc/animation/animation_host.h"
#include "cc/layers/solid_color_layer.h"
#include "cc/layers/texture_layer_client.h"
#include "cc/layers/texture_layer_impl.h"
#include "cc/test/fake_impl_task_runner_provider.h"
#include "cc/test/fake_layer_tree_frame_sink.h"
#include "cc/test/fake_layer_tree_host_client.h"
#include "cc/test/fake_layer_tree_host_impl.h"
#include "cc/test/layer_test_common.h"
#include "cc/test/layer_tree_test.h"
#include "cc/test/stub_layer_tree_host_single_thread_client.h"
#include "cc/test/test_layer_tree_frame_sink.h"
#include "cc/test/test_task_graph_runner.h"
#include "cc/trees/layer_tree_host.h"
#include "cc/trees/layer_tree_impl.h"
#include "cc/trees/single_thread_proxy.h"
#include "components/viz/common/gpu/raster_context_provider.h"
#include "components/viz/common/quads/texture_draw_quad.h"
#include "components/viz/common/resources/returned_resource.h"
#include "components/viz/common/resources/transferable_resource.h"
#include "components/viz/common/surfaces/parent_local_surface_id_allocator.h"
#include "components/viz/service/display/software_output_device.h"
#include "components/viz/test/fake_output_surface.h"
#include "gpu/GLES2/gl2extchromium.h"
#include "gpu/command_buffer/client/shared_image_interface.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::InvokeWithoutArgs;
using ::testing::Mock;

// TODO(crbug.com/40883999): settings new expectations after
// VerifyAndClearExpectations is undefined behavior. See
// http://google.github.io/googletest/gmock_cook_book.html#forcing-a-verification
#define EXPECT_SET_NEEDS_COMMIT(expect, code_to_test)                 \
  do {                                                                \
    EXPECT_CALL(*layer_tree_host_, SetNeedsCommit()).Times((expect)); \
    code_to_test;                                                     \
    Mock::VerifyAndClearExpectations(layer_tree_host_.get());         \
  } while (false)

namespace cc {
namespace {

// Compares SyncToken ignoring verified_flush() bit.
MATCHER_P(SameSyncToken, other, "") {
  gpu::SyncToken a = arg;
  gpu::SyncToken b = other;
  a.SetVerifyFlush();
  b.SetVerifyFlush();
  return a == b;
}

gpu::SyncToken GenSyncToken() {
  static int next_release = 1;
  return gpu::SyncToken(gpu::CommandBufferNamespace::GPU_IO,
                        gpu::CommandBufferId::FromUnsafeValue(0x234),
                        next_release++);
}

viz::TransferableResource MakeFakeResource() {
  return viz::TransferableResource::Make(
      gpu::ClientSharedImage::CreateForTesting(),
      viz::TransferableResource::ResourceSource::kTest, GenSyncToken());
}

viz::TransferableResource MakeFakeSoftwareResource() {
  // Generate verified tokens, as (a)
  // ClientResourceProvider::PrepareSendToParent() does not verify tokens for
  // software resources, and (b) when these tests are run with TreesInViz the
  // tokens go through serialization, which enforces the invariant that they be
  // verified.
  auto sync_token = GenSyncToken();
  sync_token.SetVerifyFlush();

  return viz::TransferableResource::Make(
      gpu::ClientSharedImage::CreateSoftwareForTesting(),
      viz::TransferableResource::ResourceSource::kTest, sync_token);
}

class MockLayerTreeHost : public LayerTreeHost {
 public:
  static std::unique_ptr<MockLayerTreeHost> Create(
      FakeLayerTreeHostClient* client,
      TaskGraphRunner* task_graph_runner,
      MutatorHost* mutator_host) {
    LayerTreeHost::InitParams params;
    params.client = client;
    params.task_graph_runner = task_graph_runner;
    params.mutator_host = mutator_host;
    LayerTreeSettings settings;
    params.settings = &settings;
    return base::WrapUnique(new MockLayerTreeHost(std::move(params)));
  }

  MOCK_METHOD0(SetNeedsCommit, void());
  MOCK_METHOD0(StartRateLimiter, void());
  MOCK_METHOD0(StopRateLimiter, void());

 private:
  explicit MockLayerTreeHost(LayerTreeHost::InitParams params)
      : LayerTreeHost(std::move(params), CompositorMode::SINGLE_THREADED) {
    InitializeSingleThreaded(&single_thread_client_,
                             base::SingleThreadTaskRunner::GetCurrentDefault());
  }

  StubLayerTreeHostSingleThreadClient single_thread_client_;
};

class MockReleaseCallback {
 public:
  MOCK_METHOD2(Release,
               void(const gpu::SyncToken& sync_token, bool lost_resource));
};

struct CommonResourceObjects {
  explicit CommonResourceObjects(bool software) {
    if (software) {
      resource = MakeFakeSoftwareResource();
    } else {
      resource = MakeFakeResource();
    }

    creation_sync_token = resource.sync_token();

    release_callback = base::BindRepeating(&MockReleaseCallback::Release,
                                           base::Unretained(&mock_callback));
  }

  CommonResourceObjects& ExpectReleaseWithSyncToken(
      const gpu::SyncToken& sync_token,
      bool lost) {
    EXPECT_CALL(mock_callback, Release(sync_token, lost)).Times(1);
    return *this;
  }

  CommonResourceObjects& ExpectRelease() {
    EXPECT_CALL(mock_callback,
                Release(SameSyncToken(creation_sync_token), false))
        .Times(1);
    return *this;
  }

  CommonResourceObjects& ExpectNoRelease() {
    EXPECT_CALL(mock_callback, Release(_, _)).Times(0);
    return *this;
  }

  void Verify() { Mock::VerifyAndClearExpectations(&mock_callback); }

  using RepeatingReleaseCallback =
      base::RepeatingCallback<void(const gpu::SyncToken& sync_token,
                                   bool is_lost)>;

  RepeatingReleaseCallback release_callback;
  viz::TransferableResource resource;
  gpu::SyncToken creation_sync_token;
  MockReleaseCallback mock_callback;
};

class TextureLayerTest : public testing::Test {
 public:
  TextureLayerTest()
      : layer_tree_frame_sink_(FakeLayerTreeFrameSink::Create3d()),
        host_impl_(&task_runner_provider_, &task_graph_runner_) {}

 protected:
  void SetUp() override {
    animation_host_ = AnimationHost::CreateForTesting(ThreadInstance::kMain);
    layer_tree_host_ = MockLayerTreeHost::Create(
        &fake_client_, &task_graph_runner_, animation_host_.get());
    EXPECT_CALL(*layer_tree_host_, SetNeedsCommit()).Times(AnyNumber());
    layer_tree_host_->SetViewportRectAndScale(gfx::Rect(10, 10), 1.f,
                                              viz::LocalSurfaceId());
    Mock::VerifyAndClearExpectations(layer_tree_host_.get());
  }

  void TearDown() override {
    Mock::VerifyAndClearExpectations(layer_tree_host_.get());
    EXPECT_CALL(*layer_tree_host_, SetNeedsCommit()).Times(AnyNumber());

    animation_host_->SetMutatorHostClient(nullptr);
    layer_tree_host_->SetRootLayer(nullptr);
    layer_tree_host_ = nullptr;
    animation_host_ = nullptr;
  }

  std::unique_ptr<MockLayerTreeHost> layer_tree_host_;
  std::unique_ptr<AnimationHost> animation_host_;
  FakeImplTaskRunnerProvider task_runner_provider_;
  FakeLayerTreeHostClient fake_client_;
  TestTaskGraphRunner task_graph_runner_;
  std::unique_ptr<LayerTreeFrameSink> layer_tree_frame_sink_;
  FakeLayerTreeHostImpl host_impl_;
  CommonResourceObjects test_resource1_{false};
  CommonResourceObjects test_resource2_{false};
  CommonResourceObjects test_resource_sw_{true};
};

TEST_F(TextureLayerTest, CheckPropertyChangeCausesCorrectBehavior) {
  scoped_refptr<TextureLayer> test_layer = TextureLayer::Create(nullptr);
  EXPECT_SET_NEEDS_COMMIT(1, layer_tree_host_->SetRootLayer(test_layer));

  // Test properties that should call SetNeedsCommit.  All properties need to
  // be set to new values in order for SetNeedsCommit to be called.
  EXPECT_SET_NEEDS_COMMIT(
      1, test_layer->SetFilterQuality(PaintFlags::FilterQuality::kNone));
  EXPECT_SET_NEEDS_COMMIT(1, test_layer->SetDynamicRangeLimit(

                                 PaintFlags::DynamicRangeLimitMixture(
                                     PaintFlags::DynamicRangeLimit::kStandard)

                                     ));
  EXPECT_SET_NEEDS_COMMIT(1, test_layer->SetUV(gfx::PointF(0.25f, 0.25f),
                                               gfx::PointF(0.75f, 0.75f)));
  EXPECT_SET_NEEDS_COMMIT(1, test_layer->SetBlendBackgroundColor(true));
}

class RunOnCommitLayerTreeHostClient : public FakeLayerTreeHostClient {
 public:
  void set_run_on_commit_and_draw(base::OnceClosure c) {
    run_on_commit_and_draw_ = std::move(c);
  }

  void DidCommitAndDrawFrame(int source_frame_number) override {
    if (run_on_commit_and_draw_)
      std::move(run_on_commit_and_draw_).Run();
  }

 private:
  base::OnceClosure run_on_commit_and_draw_;
};

// If the compositor is destroyed while TextureLayer has a resource in it, the
// resource should be returned to the client. https://crbug.com/857262
TEST_F(TextureLayerTest, ShutdownWithResource) {
  for (int i = 0; i < 2; ++i) {
    bool gpu = i == 0;
    SCOPED_TRACE(gpu);
    // Make our own LayerTreeHost for this test so we can control the lifetime.
    StubLayerTreeHostSingleThreadClient single_thread_client;
    RunOnCommitLayerTreeHostClient client;
    LayerTreeHost::InitParams params;
    params.client = &client;
    params.task_graph_runner = &task_graph_runner_;
    params.mutator_host = animation_host_.get();
    LayerTreeSettings settings;
    params.settings = &settings;
    params.main_task_runner = base::SingleThreadTaskRunner::GetCurrentDefault();
    auto host = LayerTreeHost::CreateSingleThreaded(&single_thread_client,
                                                    std::move(params));

    client.SetLayerTreeHost(host.get());
    client.SetUseSoftwareCompositing(!gpu);

    scoped_refptr<TextureLayer> layer = TextureLayer::Create(nullptr);
    layer->SetIsDrawable(true);
    layer->SetBounds(gfx::Size(10, 10));

    auto& test_resource = gpu ? test_resource1_ : test_resource_sw_;

    layer->SetTransferableResource(test_resource.resource,
                                   test_resource.release_callback);

    viz::ParentLocalSurfaceIdAllocator allocator;
    allocator.GenerateId();
    host->SetViewportRectAndScale(gfx::Rect(10, 10), 1.f,
                                  allocator.GetCurrentLocalSurfaceId());
    host->SetVisible(true);
    host->SetRootLayer(layer);

    // Commit and activate the TransferableResource in the TextureLayer.
    {
      base::RunLoop loop;
      client.set_run_on_commit_and_draw(loop.QuitClosure());
      loop.Run();
    }

    client.SetLayerTreeHost(nullptr);
    // Destroy the LayerTreeHost and the compositor-thread LayerImpl trees
    // while the resource is still in the layer. The resource should be released
    // back to the TextureLayer's client, but is post-tasked back so...
    host = nullptr;

    // We have to wait for the posted ReleaseCallback to run.
    // Our LayerTreeHostClient makes a FakeLayerTreeFrameSink which returns all
    // resources when its detached, so the resources will not be in use in the
    // display compositor, and will be returned as not lost.
    test_resource.ExpectRelease();
    {
      base::RunLoop loop;
      loop.RunUntilIdle();
    }
  }
}

class TestMailboxHolder : public TextureLayer::TransferableResourceHolder {
 public:
  using TextureLayer::TransferableResourceHolder::Create;

 protected:
  ~TestMailboxHolder() override = default;
};

class TextureLayerWithResourceTest : public TextureLayerTest {
 protected:
  void TearDown() override {
    test_resource1_.Verify();
    test_resource1_.ExpectRelease();
    TextureLayerTest::TearDown();
  }
};

TEST_F(TextureLayerWithResourceTest, ReplaceMailboxOnMainThreadBeforeCommit) {
  scoped_refptr<TextureLayer> test_layer = TextureLayer::Create(nullptr);
  ASSERT_TRUE(test_layer.get());

  EXPECT_CALL(*layer_tree_host_, SetNeedsCommit()).Times(AnyNumber());
  layer_tree_host_->SetRootLayer(test_layer);
  Mock::VerifyAndClearExpectations(layer_tree_host_.get());

  EXPECT_CALL(*layer_tree_host_, SetNeedsCommit()).Times(AtLeast(1));
  test_layer->SetTransferableResource(test_resource1_.resource,
                                      test_resource1_.release_callback);
  Mock::VerifyAndClearExpectations(layer_tree_host_.get());

  EXPECT_CALL(*layer_tree_host_, SetNeedsCommit()).Times(AtLeast(1));
  test_resource1_.ExpectRelease();
  test_layer->SetTransferableResource(test_resource2_.resource,
                                      test_resource2_.release_callback);
  Mock::VerifyAndClearExpectations(layer_tree_host_.get());
  test_resource1_.Verify();

  EXPECT_CALL(*layer_tree_host_, SetNeedsCommit()).Times(AtLeast(1));
  test_resource2_.ExpectRelease();
  test_layer->ClearTexture();
  Mock::VerifyAndClearExpectations(layer_tree_host_.get());
  test_resource2_.Verify();

  EXPECT_CALL(*layer_tree_host_, SetNeedsCommit()).Times(AtLeast(1));
  test_layer->SetTransferableResource(test_resource1_.resource,
                                      test_resource1_.release_callback);
  Mock::VerifyAndClearExpectations(layer_tree_host_.get());

  EXPECT_CALL(*layer_tree_host_, SetNeedsCommit()).Times(AtLeast(1));
  test_resource1_.ExpectRelease();
  test_layer->ClearTexture();
  Mock::VerifyAndClearExpectations(layer_tree_host_.get());
  test_resource1_.Verify();

  // Test destructor.
  EXPECT_CALL(*layer_tree_host_, SetNeedsCommit()).Times(AtLeast(1));
  test_layer->SetTransferableResource(test_resource1_.resource,
                                      test_resource1_.release_callback);
}

TEST_F(TextureLayerWithResourceTest, AffectedByHdr) {
  scoped_refptr<TextureLayer> test_layer = TextureLayer::Create(nullptr);
  ASSERT_TRUE(test_layer.get());
  EXPECT_CALL(*layer_tree_host_, SetNeedsCommit()).Times(AnyNumber());
  layer_tree_host_->SetRootLayer(test_layer);
  Mock::VerifyAndClearExpectations(layer_tree_host_.get());
  EXPECT_CALL(*layer_tree_host_, SetNeedsCommit()).Times(AtLeast(1));

  // sRGB is unaffected by HDR parameters.
  test_resource1_.resource.color_space = gfx::ColorSpace::CreateSRGB();
  test_layer->SetTransferableResource(test_resource1_.resource,
                                      test_resource1_.release_callback);
  Mock::VerifyAndClearExpectations(layer_tree_host_.get());
  EXPECT_FALSE(test_layer->RequiresSetNeedsDisplayOnHdrHeadroomChange());
  EXPECT_CALL(*layer_tree_host_, SetNeedsCommit()).Times(AtLeast(1));
  test_resource1_.ExpectRelease();

  // HDR10 is affected by HDR parameters.
  test_resource2_.resource.color_space = gfx::ColorSpace::CreateHDR10();
  test_layer->SetTransferableResource(test_resource2_.resource,
                                      test_resource2_.release_callback);
  Mock::VerifyAndClearExpectations(layer_tree_host_.get());

  EXPECT_TRUE(test_layer->RequiresSetNeedsDisplayOnHdrHeadroomChange());
  test_resource2_.ExpectRelease();
  EXPECT_CALL(*layer_tree_host_, SetNeedsCommit()).Times(AtLeast(1));

  // sRGB with extended range is affected by HDR parameters.
  test_resource1_.resource.hdr_metadata.extended_range.emplace(5.f, 5.f);
  test_layer->SetTransferableResource(test_resource1_.resource,
                                      test_resource1_.release_callback);
  Mock::VerifyAndClearExpectations(layer_tree_host_.get());
  EXPECT_TRUE(test_layer->RequiresSetNeedsDisplayOnHdrHeadroomChange());
}

class TextureLayerMailboxHolderTest : public TextureLayerTest {
 public:
  TextureLayerMailboxHolderTest() : main_thread_("MAIN") {
    main_thread_.Start();
    sync_token1_ = GenSyncToken();
    sync_token2_ = GenSyncToken();
  }

  void Wait(const base::Thread& thread) {
    base::WaitableEvent event(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                              base::WaitableEvent::InitialState::NOT_SIGNALED);
    thread.task_runner()->PostTask(
        FROM_HERE,
        base::BindOnce(&base::WaitableEvent::Signal, base::Unretained(&event)));
    event.Wait();
  }

  void CreateMainRef() {
    resource_holder_ = TestMailboxHolder::Create(
        test_resource1_.resource, test_resource1_.release_callback);
  }

  void ReleaseMainRef() { resource_holder_ = nullptr; }

  void CreateImplRef(
      viz::ReleaseCallback* impl_ref,
      scoped_refptr<base::SequencedTaskRunner> main_thread_task_runner) {
    *impl_ref =
        base::BindOnce(&TextureLayer::TransferableResourceHolder::Return,
                       resource_holder_, main_thread_task_runner);
  }

 protected:
  scoped_refptr<TextureLayer::TransferableResourceHolder> resource_holder_;
  base::Thread main_thread_;
  gpu::SyncToken sync_token1_;
  gpu::SyncToken sync_token2_;
};

TEST_F(TextureLayerMailboxHolderTest, TwoCompositors_BothReleaseThenMain) {
  scoped_refptr<TextureLayer> test_layer = TextureLayer::Create(nullptr);
  ASSERT_TRUE(test_layer.get());

  main_thread_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&TextureLayerMailboxHolderTest::CreateMainRef,
                                base::Unretained(this)));

  Wait(main_thread_);

  // The texture layer is attached to compositor1, and passes a reference to its
  // impl tree.
  viz::ReleaseCallback compositor1;
  main_thread_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&TextureLayerMailboxHolderTest::CreateImplRef,
                                base::Unretained(this), &compositor1,
                                main_thread_.task_runner()));

  // Then the texture layer is removed and attached to compositor2, and passes a
  // reference to its impl tree.
  viz::ReleaseCallback compositor2;
  main_thread_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&TextureLayerMailboxHolderTest::CreateImplRef,
                                base::Unretained(this), &compositor2,
                                main_thread_.task_runner()));

  Wait(main_thread_);
  test_resource1_.Verify();

  // The compositors both destroy their impl trees before the main thread layer
  // is destroyed.
  std::move(compositor1).Run(sync_token1_, false);
  std::move(compositor2).Run(sync_token2_, false);

  Wait(main_thread_);

  test_resource1_.ExpectNoRelease().Verify();

  // The main thread ref is the last one, so the resource is released back to
  // the embedder, with the last sync point provided by the impl trees.
  test_resource1_.ExpectReleaseWithSyncToken(sync_token2_, false);

  main_thread_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&TextureLayerMailboxHolderTest::ReleaseMainRef,
                                base::Unretained(this)));
  Wait(main_thread_);
  test_resource1_.Verify();
}

TEST_F(TextureLayerMailboxHolderTest, TwoCompositors_MainReleaseBetween) {
  scoped_refptr<TextureLayer> test_layer = TextureLayer::Create(nullptr);
  ASSERT_TRUE(test_layer.get());

  main_thread_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&TextureLayerMailboxHolderTest::CreateMainRef,
                                base::Unretained(this)));

  Wait(main_thread_);

  // The texture layer is attached to compositor1, and passes a reference to its
  // impl tree.
  viz::ReleaseCallback compositor1;
  main_thread_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&TextureLayerMailboxHolderTest::CreateImplRef,
                                base::Unretained(this), &compositor1,
                                main_thread_.task_runner()));

  // Then the texture layer is removed and attached to compositor2, and passes a
  // reference to its impl tree.
  viz::ReleaseCallback compositor2;
  main_thread_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&TextureLayerMailboxHolderTest::CreateImplRef,
                                base::Unretained(this), &compositor2,
                                main_thread_.task_runner()));

  Wait(main_thread_);
  test_resource1_.ExpectNoRelease().Verify();

  // One compositor destroys their impl tree.
  std::move(compositor1).Run(sync_token1_, false);

  // Then the main thread reference is destroyed.
  main_thread_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&TextureLayerMailboxHolderTest::ReleaseMainRef,
                                base::Unretained(this)));

  Wait(main_thread_);

  test_resource1_.ExpectNoRelease().Verify();
  // The second impl reference is destroyed last, causing the resource to be
  // released back to the embedder with the last sync point from the impl tree.
  test_resource1_.ExpectReleaseWithSyncToken(sync_token2_, true);

  std::move(compositor2).Run(sync_token2_, true);
  Wait(main_thread_);
  test_resource1_.Verify();
}

TEST_F(TextureLayerMailboxHolderTest, TwoCompositors_MainReleasedFirst) {
  scoped_refptr<TextureLayer> test_layer = TextureLayer::Create(nullptr);
  ASSERT_TRUE(test_layer.get());

  main_thread_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&TextureLayerMailboxHolderTest::CreateMainRef,
                                base::Unretained(this)));

  Wait(main_thread_);

  // The texture layer is attached to compositor1, and passes a reference to its
  // impl tree.
  viz::ReleaseCallback compositor1;
  main_thread_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&TextureLayerMailboxHolderTest::CreateImplRef,
                                base::Unretained(this), &compositor1,
                                main_thread_.task_runner()));

  // Then the texture layer is removed and attached to compositor2, and passes a
  // reference to its impl tree.
  viz::ReleaseCallback compositor2;
  main_thread_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&TextureLayerMailboxHolderTest::CreateImplRef,
                                base::Unretained(this), &compositor2,
                                main_thread_.task_runner()));

  Wait(main_thread_);
  test_resource1_.ExpectNoRelease().Verify();

  // The main thread reference is destroyed first.
  main_thread_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&TextureLayerMailboxHolderTest::ReleaseMainRef,
                                base::Unretained(this)));

  // One compositor destroys their impl tree.
  std::move(compositor2).Run(sync_token2_, false);

  Wait(main_thread_);

  test_resource1_.ExpectNoRelease().Verify();

  // The second impl reference is destroyed last, causing the resource to be
  // released back to the embedder with the last sync point from the impl tree.
  test_resource1_.ExpectReleaseWithSyncToken(sync_token1_, true);

  std::move(compositor1).Run(sync_token1_, true);
  Wait(main_thread_);
  test_resource1_.Verify();
}

class TextureLayerImplWithMailboxThreadedCallback : public LayerTreeTest {
 public:
  std::unique_ptr<TestLayerTreeFrameSink> CreateLayerTreeFrameSink(
      const viz::RendererSettings& renderer_settings,
      double refresh_rate,
      scoped_refptr<viz::RasterContextProvider> compositor_context_provider,
      scoped_refptr<viz::RasterContextProvider> worker_context_provider)
      override {
    constexpr bool disable_display_vsync = false;
    bool synchronous_composite =
        !HasImplThread() &&
        !layer_tree_host()->GetSettings().single_thread_proxy_scheduler;
    return std::make_unique<TestLayerTreeFrameSink>(
        compositor_context_provider, std::move(worker_context_provider),
        /*shared_image_interface=*/nullptr, renderer_settings, &debug_settings_,
        task_runner_provider(), synchronous_composite, disable_display_vsync,
        refresh_rate);
  }

  void AdvanceTestCase() {
    ++test_case_;
    switch (test_case_) {
      case 1:
        // Case #1: change resource before the commit. The old resource should
        // be released immediately.
        SetNewFakeResource();
        EXPECT_EQ(1, callback_count_);
        PostSetNeedsCommitToMainThread();

        // Case 2 does not rely on callbacks to advance.
        pending_callback_ = false;
        break;
      case 2:
        // Case #2: change resource after the commit (and draw), where the
        // layer draws. The old resource should be released during the next
        // commit.
        SetNewFakeResource();
        EXPECT_EQ(1, callback_count_);

        // Cases 3-5 rely on a callback to advance.
        pending_callback_ = true;
        break;
      case 3:
        EXPECT_EQ(2, callback_count_);
        // Case #3: change resource when the layer doesn't draw. The old
        // resource should be released during the next commit.
        layer_->SetBounds(gfx::Size());
        SetNewFakeResource();
        break;
      case 4:
        EXPECT_EQ(3, callback_count_);
        // Case #4: release resource that was committed but never drawn. The
        // old resource should be released during the next commit.
        layer_->ClearTexture();
        break;
      case 5:
        EXPECT_EQ(4, callback_count_);
        // Restore a resource for the next step.
        SetNewFakeResource();

        // Cases 6 and 7 do not rely on callbacks to advance.
        pending_callback_ = false;
        break;
      case 6:
        // Case #5: remove layer from tree. Callback should *not* be called, the
        // resource is returned to the main thread.
        EXPECT_EQ(4, callback_count_);
        layer_->RemoveFromParent();
        break;
      case 7:
        EXPECT_EQ(4, callback_count_);
        // Resetting the resource will call the callback now, before another
        // commit is needed, as the ReleaseCallback is already in flight from
        // RemoveFromParent().
        pending_callback_ = true;
        layer_->ClearTexture();
        frame_number_ = layer_tree_host()->SourceFrameNumber();
        break;
      case 8:
        // A commit wasn't needed, the ReleaseCallback was already in flight.
        EXPECT_EQ(frame_number_, layer_tree_host()->SourceFrameNumber());
        EXPECT_EQ(5, callback_count_);
        EndTest();
        break;
      default:
        NOTREACHED();
    }
  }

  // Make sure callback is received on main and doesn't block the impl thread.
  void ReleaseCallback(const gpu::SyncToken& sync_token, bool lost_resource) {
    EXPECT_EQ(true, main_thread_.CalledOnValidThread());
    EXPECT_FALSE(lost_resource);
    ++callback_count_;

    // If we are waiting on a callback, advance now.
    if (pending_callback_) {
      layer_tree_host()
          ->GetTaskRunnerProvider()
          ->MainThreadTaskRunner()
          ->PostTask(
              FROM_HERE,
              base::BindOnce(
                  &TextureLayerImplWithMailboxThreadedCallback::AdvanceTestCase,
                  base::Unretained(this)));
    }
  }

  void SetNewFakeResource() {
    EXPECT_EQ(true, main_thread_.CalledOnValidThread());
    viz::ReleaseCallback callback = base::BindOnce(
        &TextureLayerImplWithMailboxThreadedCallback::ReleaseCallback,
        base::Unretained(this));

    auto resource = MakeFakeResource();
    layer_->SetTransferableResource(resource, std::move(callback));
    // Damage the layer so we send a new frame with the new resource to the
    // Display compositor.
    layer_->SetNeedsDisplay();
  }

  void BeginTest() override {
    EXPECT_EQ(true, main_thread_.CalledOnValidThread());

    gfx::Size bounds(100, 100);
    root_ = Layer::Create();
    root_->SetBounds(bounds);

    layer_ = TextureLayer::Create(nullptr);
    layer_->SetIsDrawable(true);
    layer_->SetBounds(bounds);

    root_->AddChild(layer_);
    layer_tree_host()->SetRootLayer(root_);
    layer_tree_host()->SetViewportRectAndScale(gfx::Rect(bounds), 1.f,
                                               viz::LocalSurfaceId());
    SetNewFakeResource();
    EXPECT_EQ(0, callback_count_);

    // Setup is complete - advance to test case 1.
    AdvanceTestCase();
  }

  void DidCommit() override {
    // If we are not waiting on a callback, advance now.
    if (!pending_callback_)
      AdvanceTestCase();
  }

 private:
  base::ThreadChecker main_thread_;
  int callback_count_ = 0;
  int test_case_ = 0;
  int frame_number_ = 0;
  // Whether we are waiting on a callback to advance the test case.
  bool pending_callback_ = false;
  scoped_refptr<Layer> root_;
  scoped_refptr<TextureLayer> layer_;
};

SINGLE_AND_MULTI_THREAD_TEST_F(TextureLayerImplWithMailboxThreadedCallback);

class TextureLayerImplWithResourceTest : public TextureLayerTest {
 protected:
  void SetUp() override {
    TextureLayerTest::SetUp();
    layer_tree_host_ = MockLayerTreeHost::Create(
        &fake_client_, &task_graph_runner_, animation_host_.get());
    host_impl_.SetVisible(true);
    EXPECT_TRUE(host_impl_.InitializeFrameSink(layer_tree_frame_sink_.get()));
  }

  std::unique_ptr<TextureLayerImpl> CreateTextureLayer() {
    auto layer = TextureLayerImpl::Create(host_impl_.active_tree(), 1);
    layer->SetVisibleLayerRectForTesting(gfx::Rect(100, 100));
    return layer;
  }

  bool WillDraw(TextureLayerImpl* layer, DrawMode mode) {
    bool will_draw =
        layer->WillDraw(mode, host_impl_.active_tree()->resource_provider());
    if (will_draw)
      layer->DidDraw(host_impl_.active_tree()->resource_provider());
    return will_draw;
  }

  FakeLayerTreeHostClient fake_client_;
};

// Test conditions for results of TextureLayerImpl::WillDraw under
// different configurations of different mailbox, texture_id, and draw_mode.
TEST_F(TextureLayerImplWithResourceTest, TestWillDraw) {
  // Hardware mode.
  {
    std::unique_ptr<TextureLayerImpl> impl_layer = CreateTextureLayer();
    impl_layer->SetTransferableResource(test_resource1_.resource,
                                        test_resource1_.release_callback);
    EXPECT_TRUE(WillDraw(impl_layer.get(), DRAW_MODE_HARDWARE));
  }

  {
    std::unique_ptr<TextureLayerImpl> impl_layer =
        TextureLayerImpl::Create(host_impl_.active_tree(), 1);
    impl_layer->SetTransferableResource(viz::TransferableResource(),
                                        viz::ReleaseCallback());
    EXPECT_FALSE(WillDraw(impl_layer.get(), DRAW_MODE_HARDWARE));
  }

  // Software mode.
  {
    std::unique_ptr<TextureLayerImpl> impl_layer = CreateTextureLayer();
    impl_layer->SetTransferableResource(test_resource1_.resource,
                                        test_resource1_.release_callback);
    EXPECT_FALSE(WillDraw(impl_layer.get(), DRAW_MODE_SOFTWARE));
  }

  {
    std::unique_ptr<TextureLayerImpl> impl_layer = CreateTextureLayer();
    impl_layer->SetTransferableResource(viz::TransferableResource(),
                                        viz::ReleaseCallback());
    EXPECT_FALSE(WillDraw(impl_layer.get(), DRAW_MODE_SOFTWARE));
  }

  {
    // Software resource.
    std::unique_ptr<TextureLayerImpl> impl_layer = CreateTextureLayer();
    impl_layer->SetTransferableResource(test_resource_sw_.resource,
                                        test_resource_sw_.release_callback);
    EXPECT_TRUE(WillDraw(impl_layer.get(), DRAW_MODE_SOFTWARE));
  }

  // Resourceless software mode.
  {
    std::unique_ptr<TextureLayerImpl> impl_layer = CreateTextureLayer();
    impl_layer->SetTransferableResource(test_resource1_.resource,
                                        test_resource1_.release_callback);
    EXPECT_FALSE(WillDraw(impl_layer.get(), DRAW_MODE_RESOURCELESS_SOFTWARE));
  }
}

TEST_F(TextureLayerImplWithResourceTest, TestImplLayerCallbacks) {
  host_impl_.CreatePendingTree();
  std::unique_ptr<TextureLayerImpl> pending_layer;
  pending_layer = TextureLayerImpl::Create(host_impl_.pending_tree(), 1);
  ASSERT_TRUE(pending_layer);

  std::unique_ptr<LayerImpl> active_layer(
      pending_layer->CreateLayerImpl(host_impl_.active_tree()));
  ASSERT_TRUE(active_layer);

  pending_layer->SetTransferableResource(test_resource1_.resource,
                                         test_resource1_.release_callback);

  // Test multiple commits without an activation. The resource wasn't used so
  // the original sync token is returned.
  test_resource1_.ExpectRelease();
  pending_layer->SetTransferableResource(test_resource2_.resource,
                                         test_resource2_.release_callback);
  test_resource1_.Verify();

  // Test callback after activation.
  pending_layer->PushPropertiesTo(active_layer.get());
  active_layer->DidBecomeActive();

  test_resource1_.ExpectNoRelease();
  pending_layer->SetTransferableResource(test_resource1_.resource,
                                         test_resource1_.release_callback);
  test_resource1_.Verify();

  test_resource2_.ExpectRelease();
  pending_layer->PushPropertiesTo(active_layer.get());
  active_layer->DidBecomeActive();
  test_resource2_.Verify();

  // Test resetting the mailbox.
  test_resource1_.ExpectRelease();
  pending_layer->SetTransferableResource(viz::TransferableResource(),
                                         viz::ReleaseCallback());
  pending_layer->PushPropertiesTo(active_layer.get());
  active_layer->DidBecomeActive();
  test_resource1_.Verify();

  // Test destructor. The resource wasn't used so the original sync token is
  // returned.
  test_resource1_.ExpectRelease();
  pending_layer->SetTransferableResource(test_resource1_.resource,
                                         test_resource1_.release_callback);
}

TEST_F(TextureLayerImplWithResourceTest,
       TestDestructorCallbackOnCreatedResource) {
  std::unique_ptr<TextureLayerImpl> impl_layer = CreateTextureLayer();
  ASSERT_TRUE(impl_layer);

  test_resource1_.ExpectRelease();
  impl_layer->SetTransferableResource(test_resource1_.resource,
                                      test_resource1_.release_callback);
  impl_layer->DidBecomeActive();
  EXPECT_TRUE(impl_layer->WillDraw(
      DRAW_MODE_HARDWARE, host_impl_.active_tree()->resource_provider()));
  impl_layer->DidDraw(host_impl_.active_tree()->resource_provider());
  impl_layer->SetTransferableResource(viz::TransferableResource(),
                                      viz::ReleaseCallback());
}

// Checks that TextureLayer::Update does not cause an extra commit when setting
// the texture mailbox.
class TextureLayerNoExtraCommitForMailboxTest : public LayerTreeTest,
                                                public TextureLayerClient {
 public:
  // TextureLayerClient implementation.
  bool PrepareTransferableResource(
      viz::TransferableResource* resource,
      viz::ReleaseCallback* release_callback) override {
    if (layer_tree_host()->SourceFrameNumber() == 1) {
      // Once this has been committed, the resource will be released.
      *resource = viz::TransferableResource();
      return true;
    }

    *resource = MakeFakeResource();
    *release_callback = base::BindOnce(
        &TextureLayerNoExtraCommitForMailboxTest::ResourceReleased,
        base::Unretained(this));
    return true;
  }

  void ResourceReleased(const gpu::SyncToken& sync_token, bool lost_resource) {
    EXPECT_TRUE(sync_token.HasData());
    EndTest();
  }

  void SetupTree() override {
    scoped_refptr<Layer> root = Layer::Create();
    root->SetBounds(gfx::Size(10, 10));
    root->SetIsDrawable(true);

    texture_layer_ = TextureLayer::Create(this);
    texture_layer_->SetBounds(gfx::Size(10, 10));
    texture_layer_->SetIsDrawable(true);
    root->AddChild(texture_layer_);

    layer_tree_host()->SetRootLayer(root);
    LayerTreeTest::SetupTree();
  }

  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  void DidCommitAndDrawFrame() override {
    switch (layer_tree_host()->SourceFrameNumber()) {
      case 1:
        EXPECT_FALSE(proxy()->MainFrameWillHappenForTesting());
        // Invalidate the texture layer to clear the mailbox before
        // ending the test.
        texture_layer_->SetNeedsDisplay();
        break;
      case 2:
        break;
      default:
        NOTREACHED();
    }
  }

 private:
  scoped_refptr<TextureLayer> texture_layer_;
};

SINGLE_AND_MULTI_THREAD_TEST_F(TextureLayerNoExtraCommitForMailboxTest);

// Checks that changing a mailbox in the client for a TextureLayer that's
// invisible correctly works and uses the new mailbox as soon as the layer
// becomes visible (and returns the old one).
class TextureLayerChangeInvisibleMailboxTest : public LayerTreeTest,
                                               public TextureLayerClient {
 public:
  TextureLayerChangeInvisibleMailboxTest() : resource_(MakeResource('1')) {}

  // TextureLayerClient implementation.
  bool PrepareTransferableResource(
      viz::TransferableResource* resource,
      viz::ReleaseCallback* release_callback) override {
    ++prepare_called_;
    if (!resource_changed_)
      return false;
    resource_changed_ = false;
    *resource = resource_;
    *release_callback = base::BindOnce(
        &TextureLayerChangeInvisibleMailboxTest::ResourceReleased,
        base::Unretained(this));
    return true;
  }

  viz::TransferableResource MakeResource(char name) {
    return MakeFakeResource();
  }

  void ResourceReleased(const gpu::SyncToken& sync_token, bool lost_resource) {
    EXPECT_TRUE(sync_token.HasData());
    ++resource_returned_;

    if (resource_returned_ == 1) {
      // The 1st resource should be released after the 2nd is prepared.
      EXPECT_GE(prepare_called_, 2);

      // Clear the 2nd resource to let the test complete.
      MainThreadTaskRunner()->PostTask(
          FROM_HERE,
          base::BindOnce(
              &TextureLayerChangeInvisibleMailboxTest::ClearTextureLayerClient,
              base::Unretained(this)));
      return;
    }

    // The actual releasing of resources by
    // TextureLayer::TransferableResourceHolder::dtor can be done as a PostTask.
    // The test signal being used, DidPresentCompositorFrame itself is also
    // posted back from the Compositor-thread to the Main-thread. Due to this
    // there's a teardown race which tsan builds can encounter. So if
    // `close_on_resource_returned_` is set we actually end the test here.
    if (close_on_resource_returned_) {
      EXPECT_EQ(2, resource_returned_);
      EndTest();
    }
  }

  void ClearTextureLayerClient() { texture_layer_->ClearClient(); }

  void SetupTree() override {
    scoped_refptr<Layer> root = Layer::Create();
    root->SetBounds(gfx::Size(10, 10));
    root->SetIsDrawable(true);

    solid_layer_ = SolidColorLayer::Create();
    solid_layer_->SetBounds(gfx::Size(10, 10));
    solid_layer_->SetIsDrawable(true);
    solid_layer_->SetBackgroundColor(SkColors::kWhite);
    root->AddChild(solid_layer_);

    parent_layer_ = Layer::Create();
    parent_layer_->SetBounds(gfx::Size(10, 10));
    parent_layer_->SetIsDrawable(true);
    root->AddChild(parent_layer_);

    texture_layer_ = TextureLayer::Create(this);
    texture_layer_->SetBounds(gfx::Size(10, 10));
    texture_layer_->SetIsDrawable(true);
    parent_layer_->AddChild(texture_layer_);

    layer_tree_host()->SetRootLayer(root);
    LayerTreeTest::SetupTree();
  }

  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  void DidPresentCompositorFrame(
      uint32_t frame_token,
      const viz::FrameTimingDetails& frame_timing_details) override {
    ++presented_count_;
    // The fifth frame to be presented will be returning resources. Due to
    // PostTasks the ResourcesReleased callback may not yet have been called. So
    // we can only end the test here if we have received the updated
    // `resource_returned_`. Otherwise set `close_on_resources_returned_` to
    // have the callback do the teardown.
    if (presented_count_ == 5) {
      if (resource_returned_ < 2) {
        close_on_resource_returned_ = true;
      } else {
        EXPECT_EQ(2, resource_returned_);
        EndTest();
      }
    }
  }

  void DidCommitAndDrawFrame() override {
    ++commit_and_draw_count_;
    // The timing of DidPresentCompositorFrame is not guaranteed. Each of
    // these checks are actually valid immediately after frame submission, as
    // the are a part of Commit.
    switch (commit_and_draw_count_) {
      case 1:
        // We should have updated the layer, committing the texture.
        EXPECT_EQ(1, prepare_called_);
        // Make layer invisible.
        parent_layer_->SetOpacity(0.f);
        break;
      case 2:
        // Layer shouldn't have been updated.
        EXPECT_EQ(1, prepare_called_);
        // Change the texture.
        resource_ = MakeResource('2');
        resource_changed_ = true;
        texture_layer_->SetNeedsDisplay();
        // Force a change to make sure we draw a frame.
        solid_layer_->SetBackgroundColor(SkColors::kGray);
        break;
      case 3:
        // Layer shouldn't have been updated.
        EXPECT_EQ(1, prepare_called_);
        // So the old resource isn't returned yet.
        EXPECT_EQ(0, resource_returned_);
        // Make layer visible again.
        parent_layer_->SetOpacity(0.9f);
        break;
      case 4:
        // Layer should have been updated.
        // It's not sufficient to check if |prepare_called_| is 2. It's possible
        // for BeginMainFrame and hence PrepareTransferableResource to run twice
        // before DidPresentCompositorFrame due to pipelining.
        EXPECT_GE(prepare_called_, 2);
        break;
      default:
        break;
    }
  }

 private:
  scoped_refptr<SolidColorLayer> solid_layer_;
  scoped_refptr<Layer> parent_layer_;
  scoped_refptr<TextureLayer> texture_layer_;

  // Used on the main thread.
  bool resource_changed_ = true;
  viz::TransferableResource resource_;
  int resource_returned_ = 0;
  int prepare_called_ = 0;
  int presented_count_ = 0;
  int commit_and_draw_count_ = 0;
  bool close_on_resource_returned_ = false;
};

// TODO(crbug.com/40760099): Test fails on chromeos-amd64-generic-rel.
#if BUILDFLAG(IS_CHROMEOS)
#define MAYBE_SINGLE_AND_MULTI_THREAD_TEST_F MULTI_THREAD_TEST_F
#else
#define MAYBE_SINGLE_AND_MULTI_THREAD_TEST_F SINGLE_AND_MULTI_THREAD_TEST_F
#endif

MAYBE_SINGLE_AND_MULTI_THREAD_TEST_F(TextureLayerChangeInvisibleMailboxTest);

// Test that TextureLayerImpl::ReleaseResources can be called which releases
// the resource back to TextureLayerClient.
class TextureLayerReleaseResourcesBase : public LayerTreeTest,
                                         public TextureLayerClient {
 public:
  // TextureLayerClient implementation.
  bool PrepareTransferableResource(
      viz::TransferableResource* resource,
      viz::ReleaseCallback* release_callback) override {
    if (commit_count_ > 0) {
      // Any update after the first commit should clear the resource to ensure
      // the main thread layer doesn't hold onto it.
      *resource = viz::TransferableResource();
      return true;
    }

    *resource = MakeFakeResource();
    *release_callback =
        base::BindOnce(&TextureLayerReleaseResourcesBase::ResourceReleased,
                       base::Unretained(this));
    return true;
  }

  void ResourceReleased(const gpu::SyncToken& sync_token, bool lost_resource) {
    resource_released_ = true;
    // End the test when resource is released.
    if (commit_count_ >= 1) {
      EndTest();
    }
  }

  void SetupTree() override {
    LayerTreeTest::SetupTree();

    scoped_refptr<TextureLayer> texture_layer = TextureLayer::Create(this);
    texture_layer->SetBounds(gfx::Size(10, 10));
    texture_layer->SetIsDrawable(true);

    layer_tree_host()->root_layer()->AddChild(texture_layer);
    texture_layer_id_ = texture_layer->id();
  }

  void BeginTest() override {
    resource_released_ = false;
    commit_count_ = 0;
    PostSetNeedsCommitToMainThread();
  }

  void DidCommitAndDrawFrame() override {
    ++commit_count_;
    PostSetNeedsCommitToMainThread();
  }

  void AfterTest() override { EXPECT_TRUE(resource_released_); }

 protected:
  int texture_layer_id_;
  int commit_count_ = 0;

 private:
  bool resource_released_ = false;
};

class TextureLayerReleaseResourcesAfterCommit
    : public TextureLayerReleaseResourcesBase {
 public:
  void CommitCompleteOnThread(LayerTreeHostImpl* host_impl) override {
    if (commit_count_ == 0) {
      // After first commit, call ReleaseResources and verify it's released by
      // the impl layer. It'll be released by the main thread layer during the
      // next update.
      auto* texture_impl = static_cast<TextureLayerImpl*>(
          host_impl->sync_tree()->LayerById(texture_layer_id_));

      // Verify resource exists before releasing
      EXPECT_FALSE(texture_impl->transferable_resource().is_empty());

      texture_impl->ReleaseResources();

      // Verify resource was released from impl thread
      EXPECT_TRUE(texture_impl->transferable_resource().is_empty());
    }
  }
};

SINGLE_AND_MULTI_THREAD_TEST_F(TextureLayerReleaseResourcesAfterCommit);

class TextureLayerReleaseResourcesAfterActivate
    : public TextureLayerReleaseResourcesBase {
 public:
  void DidActivateTreeOnThread(LayerTreeHostImpl* host_impl) override {
    if (commit_count_ == 0) {
      // After first commit, call ReleaseResources and verify it's released by
      // the impl layer. It'll be released by the main thread layer during the
      // next update.
      auto* texture_impl = static_cast<TextureLayerImpl*>(
          host_impl->active_tree()->LayerById(texture_layer_id_));

      // Verify resource exists before releasing
      EXPECT_FALSE(texture_impl->transferable_resource().is_empty());

      texture_impl->ReleaseResources();

      // Verify resource was released from impl thread
      EXPECT_TRUE(texture_impl->transferable_resource().is_empty());
    }
  }
};

SINGLE_AND_MULTI_THREAD_TEST_F(TextureLayerReleaseResourcesAfterActivate);

class TextureLayerWithResourceMainThreadDeleted : public LayerTreeTest {
 public:
  void ReleaseCallback(const gpu::SyncToken& sync_token, bool lost_resource) {
    EXPECT_EQ(true, main_thread_.CalledOnValidThread());
    EXPECT_FALSE(lost_resource);
    ++callback_count_;
    EndTest();
  }

  void SetNewFakeResource() {
    EXPECT_EQ(true, main_thread_.CalledOnValidThread());
    viz::ReleaseCallback callback = base::BindOnce(
        &TextureLayerWithResourceMainThreadDeleted::ReleaseCallback,
        base::Unretained(this));
    auto resource = MakeFakeResource();
    layer_->SetTransferableResource(resource, std::move(callback));
  }

  void SetupTree() override {
    gfx::Size bounds(100, 100);
    root_ = Layer::Create();
    root_->SetBounds(bounds);

    layer_ = TextureLayer::Create(nullptr);
    layer_->SetIsDrawable(true);
    layer_->SetBounds(bounds);

    root_->AddChild(layer_);
    layer_tree_host()->SetRootLayer(root_);
    layer_tree_host()->SetViewportRectAndScale(gfx::Rect(bounds), 1.f,
                                               viz::LocalSurfaceId());
  }

  void BeginTest() override {
    EXPECT_EQ(true, main_thread_.CalledOnValidThread());

    callback_count_ = 0;

    // Set the resource on the main thread.
    SetNewFakeResource();
    EXPECT_EQ(0, callback_count_);

    PostSetNeedsCommitToMainThread();
  }

  void DidCommitAndDrawFrame() override {
    switch (layer_tree_host()->SourceFrameNumber()) {
      case 1:
        // Delete the TextureLayer on the main thread while the resource is in
        // the impl tree.
        layer_->RemoveFromParent();
        layer_ = nullptr;
        break;
    }
  }

  void AfterTest() override { EXPECT_EQ(1, callback_count_); }

 private:
  base::ThreadChecker main_thread_;
  int callback_count_;
  scoped_refptr<Layer> root_;
  scoped_refptr<TextureLayer> layer_;
};

SINGLE_AND_MULTI_THREAD_TEST_F(TextureLayerWithResourceMainThreadDeleted);

class TextureLayerWithResourceImplThreadDeleted : public LayerTreeTest {
 public:
  void ReleaseCallback(const gpu::SyncToken& sync_token, bool lost_resource) {
    EXPECT_EQ(true, main_thread_.CalledOnValidThread());
    EXPECT_FALSE(lost_resource);
    ++callback_count_;
    EndTest();
  }

  void SetNewFakeResource() {
    EXPECT_EQ(true, main_thread_.CalledOnValidThread());
    viz::ReleaseCallback callback = base::BindOnce(
        &TextureLayerWithResourceImplThreadDeleted::ReleaseCallback,
        base::Unretained(this));
    auto resource = MakeFakeResource();
    layer_->SetTransferableResource(resource, std::move(callback));
  }

  void SetupTree() override {
    gfx::Size bounds(100, 100);
    root_ = Layer::Create();
    root_->SetBounds(bounds);

    layer_ = TextureLayer::Create(nullptr);
    layer_->SetIsDrawable(true);
    layer_->SetBounds(bounds);

    root_->AddChild(layer_);
    layer_tree_host()->SetRootLayer(root_);
    layer_tree_host()->SetViewportRectAndScale(gfx::Rect(bounds), 1.f,
                                               viz::LocalSurfaceId());
  }

  void BeginTest() override {
    EXPECT_EQ(true, main_thread_.CalledOnValidThread());

    callback_count_ = 0;

    // Set the resource on the main thread.
    SetNewFakeResource();
    EXPECT_EQ(0, callback_count_);

    PostSetNeedsCommitToMainThread();
  }

  void DidCommitAndDrawFrame() override {
    switch (layer_tree_host()->SourceFrameNumber()) {
      case 1:
        // Remove the TextureLayer on the main thread while the resource is in
        // the impl tree, but don't delete the TextureLayer until after the impl
        // tree side is deleted.
        layer_->RemoveFromParent();
        break;
      case 2:
        layer_ = nullptr;
        break;
    }
  }

  void AfterTest() override { EXPECT_EQ(1, callback_count_); }

 private:
  base::ThreadChecker main_thread_;
  int callback_count_;
  scoped_refptr<Layer> root_;
  scoped_refptr<TextureLayer> layer_;
};

SINGLE_AND_MULTI_THREAD_TEST_F(TextureLayerWithResourceImplThreadDeleted);

class StubTextureLayerClient : public TextureLayerClient {
 public:
  // TextureLayerClient implementation.
  bool PrepareTransferableResource(
      viz::TransferableResource* resource,
      viz::ReleaseCallback* release_callback) override {
    return false;
  }
};

class SoftwareLayerTreeHostClient : public StubLayerTreeHostClient {
 public:
  SoftwareLayerTreeHostClient() = default;
  ~SoftwareLayerTreeHostClient() override = default;

  // Caller responsible for unsetting this and maintaining the host's lifetime.
  void SetLayerTreeHost(LayerTreeHost* host) { host_ = host; }

  // StubLayerTreeHostClient overrides.
  void RequestNewLayerTreeFrameSink() override {
    auto sink = FakeLayerTreeFrameSink::CreateSoftware();
    frame_sink_ = sink.get();
    host_->SetLayerTreeFrameSink(std::move(sink));
  }

  FakeLayerTreeFrameSink* frame_sink() const { return frame_sink_; }

 private:
  raw_ptr<FakeLayerTreeFrameSink> frame_sink_ = nullptr;
  raw_ptr<LayerTreeHost> host_ = nullptr;
};

class SoftwareTextureLayerTest : public LayerTreeTest {
 protected:
  SoftwareTextureLayerTest() : LayerTreeTest(viz::RendererType::kSoftware) {}

  void AfterTest() override {
    // Clear before the LayerTreeHost (and its TestLayerTreeFrameSink) is
    // destroyed to prevent a dangling pointer during test cleanup.
    frame_sink_ = nullptr;
    LayerTreeTest::AfterTest();
  }

  void SetupTree() override {
    root_ = Layer::Create();
    root_->SetBounds(gfx::Size(10, 10));

    // A drawable layer so that frames always get drawn.
    solid_color_layer_ = SolidColorLayer::Create();
    solid_color_layer_->SetIsDrawable(true);
    solid_color_layer_->SetBackgroundColor(SkColors::kRed);
    solid_color_layer_->SetBounds(gfx::Size(10, 10));
    root_->AddChild(solid_color_layer_);

    texture_layer_ = TextureLayer::Create(&client_);
    texture_layer_->SetIsDrawable(true);
    texture_layer_->SetBounds(gfx::Size(10, 10));
    layer_tree_host()->SetRootLayer(root_);
    LayerTreeTest::SetupTree();
  }

  std::unique_ptr<TestLayerTreeFrameSink> CreateLayerTreeFrameSink(
      const viz::RendererSettings& renderer_settings,
      double refresh_rate,
      scoped_refptr<viz::RasterContextProvider> compositor_context_provider,
      scoped_refptr<viz::RasterContextProvider> worker_context_provider)
      override {
    context_provider_sw_ = viz::TestContextProvider::CreateRaster();
    gpu::SharedImageInterface* shared_image_interface_sw =
        context_provider_sw_->SharedImageInterface();

    constexpr bool disable_display_vsync = false;
    bool synchronous_composite =
        !HasImplThread() &&
        !layer_tree_host()->GetSettings().single_thread_proxy_scheduler;
    auto sink = std::make_unique<TestLayerTreeFrameSink>(
        nullptr, nullptr, shared_image_interface_sw, renderer_settings,
        &debug_settings_, task_runner_provider(), synchronous_composite,
        disable_display_vsync, refresh_rate);
    frame_sink_ = sink.get();
    num_frame_sinks_created_++;
    return sink;
  }

  StubTextureLayerClient client_;
  scoped_refptr<Layer> root_;
  scoped_refptr<SolidColorLayer> solid_color_layer_;
  scoped_refptr<TextureLayer> texture_layer_;
  raw_ptr<TestLayerTreeFrameSink> frame_sink_ = nullptr;
  int num_frame_sinks_created_ = 0;

  scoped_refptr<viz::RasterContextProvider> context_provider_sw_;
};

class SoftwareTextureLayerSwitchTreesTest : public SoftwareTextureLayerTest {
 protected:
  void BeginTest() override {
    PostSetNeedsCommitToMainThread();
  }

  void DidCommitAndDrawFrame() override {
    step_ = layer_tree_host()->SourceFrameNumber();
    switch (step_) {
      case 1: {
        // The test starts by inserting the TextureLayer to the tree.
        root_->AddChild(texture_layer_);

        texture_layer_->SetTransferableResource(MakeFakeSoftwareResource(),
                                                base::DoNothing());
      } break;
      case 2:
        // When the layer is removed from the tree, the layer should be
        // unregistered.
        texture_layer_->RemoveFromParent();
        break;
      case 3:
        // When the layer is added to a new tree, the layer is registered again.
        root_->AddChild(texture_layer_);
        break;
      case 4:
        // If the layer is removed and added back to the same tree in one
        // commit, there should be no side effects, the bitmap stays
        // registered.
        texture_layer_->RemoveFromParent();
        root_->AddChild(texture_layer_);
        break;
      case 5:
        // Release the TransferableResource before shutdown.
        texture_layer_->ClearClient();
        break;
      case 6:
        EndTest();
    }
  }

  void DisplayReceivedCompositorFrameOnThread(
      const viz::CompositorFrame& frame) override {
    verified_frames_++;
  }

  void AfterTest() override {
    EXPECT_EQ(6, verified_frames_);
    SoftwareTextureLayerTest::AfterTest();
  }

  int step_ = 0;
  int verified_frames_ = 0;
};

SINGLE_AND_MULTI_THREAD_TEST_F(SoftwareTextureLayerSwitchTreesTest);

class SoftwareTextureLayerPurgeMemoryTest : public SoftwareTextureLayerTest {
 protected:
  void BeginTest() override {
    PostSetNeedsCommitToMainThread();
  }

  void DidCommitAndDrawFrame() override {
    step_ = layer_tree_host()->SourceFrameNumber();
    switch (step_) {
      case 1: {
        // The test starts by inserting the TextureLayer to the tree.
        root_->AddChild(texture_layer_);

        texture_layer_->SetTransferableResource(MakeFakeSoftwareResource(),
                                                base::DoNothing());
      }

      break;
      case 2:
        // Draw again after OnPurgeMemory() was called on the impl thread.
        texture_layer_->SetNeedsDisplay();
        break;
      case 3:
        // Release the TransferableResource before shutdown.
        texture_layer_->ClearClient();
        break;
      case 4:
        EndTest();
    }
  }

  void DrawLayersOnThread(LayerTreeHostImpl* host_impl) override {
    // TextureLayerImpl will have registered the layer at this point.
    // Call OnPurgeMemory() to ensure that the same layer doesn't get
    // registered again on the next draw.
    if (step_ == 1)
      base::MemoryPressureListener::SimulatePressureNotification(
          base::MemoryPressureListener::MEMORY_PRESSURE_LEVEL_CRITICAL);
  }

  void DisplayReceivedCompositorFrameOnThread(
      const viz::CompositorFrame& frame) override {
    verified_frames_++;
  }

  void AfterTest() override {
    EXPECT_EQ(4, verified_frames_);
    SoftwareTextureLayerTest::AfterTest();
  }

  int step_ = 0;
  int verified_frames_ = 0;
};

// Run the single thread test only.
// MemoryPressureListener::DoNotifyMemoryPressure() is called in this
// PurgeMemoryTest. Although the observation is targeted on certain
// configurations and will be dismissed later, it triggers a "CHECK failed:
// checker.CalledOnValidSequence(&bound_at)" first on the multithreading
// setting.

SINGLE_THREAD_TEST_F(SoftwareTextureLayerPurgeMemoryTest);

class SoftwareTextureLayerMultipleResourceTest
    : public SoftwareTextureLayerTest {
 protected:
  void BeginTest() override {
    PostSetNeedsCommitToMainThread();
  }

  void DidCommitAndDrawFrame() override {
    step_ = layer_tree_host()->SourceFrameNumber();
    switch (step_) {
      case 1: {
        // The test starts by inserting the TextureLayer to the tree.
        root_->AddChild(texture_layer_);

        texture_layer_->SetTransferableResource(MakeFakeSoftwareResource(),
                                                base::DoNothing());
        texture_layer_->SetTransferableResource(MakeFakeSoftwareResource(),
                                                base::DoNothing());
      } break;
      case 2:
        // Force a commit and SubmitCompositorFrame so that we can see it.
        texture_layer_->SetNeedsDisplay();
        break;
      case 3:
        // Drop the other registration.
        texture_layer_->ClearClient();
        break;
      case 4:
        EndTest();
    }
  }

  void DisplayReceivedCompositorFrameOnThread(
      const viz::CompositorFrame& frame) override {
    verified_frames_++;
  }

  void AfterTest() override {
    EXPECT_EQ(4, verified_frames_);
    SoftwareTextureLayerTest::AfterTest();
  }

  int step_ = 0;
  int verified_frames_ = 0;
};

SINGLE_AND_MULTI_THREAD_TEST_F(SoftwareTextureLayerMultipleResourceTest);

class SoftwareTextureLayerLoseFrameSinkTest : public SoftwareTextureLayerTest {
 protected:
  void BeginTest() override {
    PostSetNeedsCommitToMainThread();
  }

  void DidCommitAndDrawFrame() override {
    // We run the next step in a clean stack, so that we don't cause side
    // effects that will interfere with this current stack unwinding.
    // Specifically, removing the LayerTreeFrameSink destroys the Display
    // and the BeginFrameSource, but they can be on the stack (see
    // https://crbug.com/829484).
    MainThreadTaskRunner()->PostTask(
        FROM_HERE,
        base::BindOnce(&SoftwareTextureLayerLoseFrameSinkTest::NextStep,
                       base::Unretained(this)));
  }

  void NextStep() {
    step_ = layer_tree_host()->SourceFrameNumber();
    switch (step_) {
      case 1: {
        // The test starts by inserting the TextureLayer to the tree.
        root_->AddChild(texture_layer_);

        auto release_callback = base::BindOnce(
            &SoftwareTextureLayerLoseFrameSinkTest::ReleaseCallback,
            base::Unretained(this));

        texture_layer_->SetTransferableResource(MakeFakeSoftwareResource(),
                                                std::move(release_callback));

        EXPECT_FALSE(released_);
      } break;
      case 2:
        // The frame sink is lost. The host will make a new one and submit
        // another frame, with the id being registered again.
        layer_tree_host()->SetVisible(false);
        // Clear frame_sink_ before releasing to prevent dangling pointer. The
        // normal clear in AfterTest won't handle it as this test is
        // intentionally modifying the frame sink's lifetime.
        frame_sink_ = nullptr;
        layer_tree_host()->ReleaseLayerTreeFrameSink();
        layer_tree_host()->SetVisible(true);
        texture_layer_->SetNeedsDisplay();
        EXPECT_FALSE(released_);
        break;
      case 3:
        // Even though the frame sink was lost, the software resource given to
        // the TextureLayer was not lost/returned.
        EXPECT_FALSE(released_);
        // Release the TransferableResource before shutdown, the test ends when
        // it is released.
        texture_layer_->ClearClient();
    }
  }

  void DisplayReceivedCompositorFrameOnThread(
      const viz::CompositorFrame& frame) override {
    verified_frames_++;
  }

  void WillCommit(const CommitState& commit_state) override {
    source_frame_number_ = commit_state.source_frame_number;
  }

  void ReleaseCallback(const gpu::SyncToken& sync_token, bool lost) {
    // The software resource is not released when the LayerTreeFrameSink is lost
    // since software resources are not destroyed by the GPU process dying. It
    // is released only after we call TextureLayer::ClearClient().

    EXPECT_EQ(source_frame_number_, 3);
    released_ = true;
    EndTest();
  }

  void AfterTest() override {
    EXPECT_EQ(4, verified_frames_);
    SoftwareTextureLayerTest::AfterTest();
  }

  int step_ = 0;
  int verified_frames_ = 0;
  int source_frame_number_ = 0;
  bool released_ = false;
};

SINGLE_AND_MULTI_THREAD_TEST_F(SoftwareTextureLayerLoseFrameSinkTest);

class TextureLayerNoResourceTest : public LayerTreeTest, TextureLayerClient {
 public:
  bool PrepareTransferableResource(
      viz::TransferableResource* transferable_resource,
      viz::ReleaseCallback* release_callback) override {
    return false;
  }

  void SetupTree() override {
    SetInitialRootBounds(gfx::Size(100, 100));
    LayerTreeTest::SetupTree();
    auto texture_layer = TextureLayer::Create(this);
    texture_layer->SetIsDrawable(true);
    texture_layer->SetContentsOpaque(true);
    texture_layer->SetBounds(gfx::Size(100, 100));
    texture_layer->SetBackgroundColor(SkColors::kRed);
    layer_tree_host()->root_layer()->AddChild(texture_layer);
    texture_layer_id_ = static_cast<uint32_t>(texture_layer->id());
  }

  void BeginTest() override { PostSetNeedsCommitToMainThread(); }

  void DisplayReceivedCompositorFrameOnThread(
      const viz::CompositorFrame& frame) override {
    EXPECT_EQ(0u, frame.resource_list.size());
    EXPECT_EQ(1u, frame.render_pass_list.size());

    const auto& quad_list = frame.render_pass_list[0]->quad_list;
    EXPECT_EQ(1u, quad_list.size());
    EXPECT_NE(viz::DrawQuad::Material::kTextureContent,
              quad_list.ElementAt(0)->material);

    const auto& shared_list = frame.render_pass_list[0]->shared_quad_state_list;
    EXPECT_EQ(1u, shared_list.size());
    EXPECT_NE(texture_layer_id_, shared_list.ElementAt(0)->layer_id);

    EndTest();
  }

 private:
  uint32_t texture_layer_id_;
};

SINGLE_AND_MULTI_THREAD_TEST_F(TextureLayerNoResourceTest);

}  // namespace
}  // namespace cc
