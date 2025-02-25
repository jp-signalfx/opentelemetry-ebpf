// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include <generated/test/app1/auto_handles.h>
#include <generated/test/app1/containers.inl>
#include <generated/test/app1/handles.h>
#include <generated/test/app1/index.h>
#include <generated/test/app1/modifiers.h>
#include <generated/test/metrics.h>

#include <gtest/gtest.h>

TEST(RenderTest, AutoHandle)
{
  test::app1::Index index;

  {
    auto span = index.simple_span.alloc();
    ASSERT_TRUE(span.valid());

    ASSERT_EQ(index.simple_span.size(), 1);
  }

  ASSERT_EQ(index.simple_span.size(), 0);

  {
    auto span = index.simple_span.alloc();
    ASSERT_TRUE(span.valid());

    ASSERT_EQ(index.simple_span.size(), 1);

    span.put();
    ASSERT_FALSE(span.valid());

    ASSERT_EQ(index.simple_span.size(), 0);
  }
}

TEST(RenderTest, Handle)
{
  static constexpr u32 the_number = 42;

  test::app1::Index index;

  auto auto_handle = index.simple_span.alloc();
  ASSERT_TRUE(auto_handle.valid());

  // Set the integer field to some important number.
  auto_handle.modify().number(the_number);
  ASSERT_EQ(auto_handle.number(), the_number);

  // Convert auto-handle to handle.
  auto handle = auto_handle.to_handle();
  ASSERT_TRUE(handle.valid());

  // Auto-handle is released.
  ASSERT_FALSE(auto_handle.valid());
  ASSERT_EQ(index.simple_span.size(), 1);

  // Check that it's the same span.
  ASSERT_EQ(handle.access(index).number(), the_number);

  handle.put(index);
  ASSERT_FALSE(handle.valid());

  ASSERT_EQ(index.simple_span.size(), 0);
}

TEST(RenderTest, IndexedSpan)
{
  static constexpr u32 key = 42;

  test::app1::Index index;

  {
    auto ahandle = index.indexed_span.by_key(key);
    ASSERT_TRUE(ahandle.valid());
    ASSERT_EQ(ahandle.number(), key);

    ASSERT_EQ(index.indexed_span.size(), 1);

    {
      auto another = index.indexed_span.by_key(key);
      ASSERT_TRUE(another.valid());

      // Still only one span is allocated.
      ASSERT_EQ(index.indexed_span.size(), 1);

      // It's the same span.
      ASSERT_EQ(ahandle.loc(), another.loc());
    }

    {
      auto different = index.indexed_span.by_key(key + 1);
      ASSERT_TRUE(different.valid());

      // Additional span has been allocated.
      ASSERT_EQ(index.indexed_span.size(), 2);

      // It's not the same span.
      ASSERT_NE(ahandle.loc(), different.loc());
    }
  }

  ASSERT_EQ(index.indexed_span.size(), 0);
}

TEST(RenderTest, MetricStore)
{
  using metrics_visitor_t =
      std::function<void(u64, test::app1::weak_refs::metrics_span, test::metrics::some_metrics const &, u64)>;

  static constexpr u64 timeslot_duration = 1'000'000'000;
  u64 time_now = 1;

  test::app1::Index index;

  test::metrics::some_metrics_point input_metrics{
      .active = 55,
      .total = 100,
  };

  {
    auto span = index.metrics_span.alloc();
    ASSERT_TRUE(span.valid());

    ASSERT_EQ(index.metrics_span.size(), 1);
    ASSERT_EQ(span.refcount(), 1);

    span.metrics_update(time_now, input_metrics);

    ASSERT_EQ(index.metrics_span.size(), 1);

    // Metric store is keeping a reference to this span.
    ASSERT_EQ(span.refcount(), 2);
  }

  // Metric store is keeping the span allocated.
  ASSERT_EQ(index.metrics_span.size(), 1);

  // Metric slot should not be ready yet.
  ASSERT_FALSE(index.metrics_span.metrics_ready(time_now));

  // Advance the current time.
  time_now += 2 * timeslot_duration;

  // Metrics slot should be ready.
  ASSERT_TRUE(index.metrics_span.metrics_ready(time_now));

  // Get the metrics from the current slot.
  int metric_counter{0};
  test::metrics::some_metrics slot_metrics{0};
  metrics_visitor_t on_metric = [&metric_counter, &slot_metrics](u64 timestamp, auto span, auto metrics, u64 interval) {
    ++metric_counter;
    slot_metrics = metrics;
  };
  index.metrics_span.metrics_foreach(time_now, on_metric);

  // Only one metrics slot.
  ASSERT_EQ(metric_counter, 1);

  // Output metrics match input metrics.
  ASSERT_EQ(slot_metrics.active, input_metrics.active);
  ASSERT_EQ(slot_metrics.total, input_metrics.total);

  // Metrics store should be cleared out.
  ASSERT_TRUE(index.metrics_span.metrics.current_queue().empty());

  // Metric store should no longer keep a reference to the span.
  ASSERT_EQ(index.metrics_span.size(), 0);
}

TEST(RenderTest, ManualReference)
{
  test::app1::Index index;

  auto span = index.span_with_manual_reference.alloc();
  ASSERT_TRUE(span.valid());

  auto simple_loc = span.manual_reference().loc();

  // Currently the reference is an invalid reference.
  ASSERT_EQ(simple_loc, span.manual_reference().invalid);

  // No simple_span is allocated.
  ASSERT_EQ(index.simple_span.size(), 0);

  {
    auto s = index.simple_span.alloc();
    ASSERT_TRUE(s.valid());

    // Save the location of this newly-allocated simple_span.
    simple_loc = s.loc();

    // Assign it as the reference.
    span.modify().manual_reference(s.get());

    ASSERT_EQ(index.simple_span.size(), 1);
    ASSERT_EQ(span.manual_reference().refcount(), 2);
  }

  ASSERT_TRUE(span.manual_reference().valid());
  ASSERT_EQ(span.manual_reference().refcount(), 1);

  // It's the same simple_span.
  ASSERT_EQ(simple_loc, span.manual_reference().loc());
}

TEST(RenderTest, AutoReference)
{
  static constexpr u32 key_one = 11;
  static constexpr u32 key_two = 22;

  test::app1::Index index;

  auto indexed = index.indexed_span.by_key(key_one);
  ASSERT_TRUE(indexed.valid());
  ASSERT_EQ(indexed.number(), key_one);

  // Only one indexed_span exists for now (namely indexed_span{key_one}).
  ASSERT_EQ(index.indexed_span.size(), 1);

  auto span = index.span_with_auto_reference.alloc();
  ASSERT_TRUE(span.valid());

  // The auto-reference is not yet valid because the `number` field that is used in the reference key (see test.render)
  // has not been assigned.
  ASSERT_FALSE(span.auto_reference().valid());

  // Still only one indexed_span exists.
  ASSERT_EQ(index.indexed_span.size(), 1);

  // Assign the field that is used in the reference key.
  span.modify().number(key_two);

  // This caused the reference to be computed and a new indexed_span to be allocated (indexed_span{key_two}).
  ASSERT_EQ(index.indexed_span.size(), 2);

  // Now the reference is valid.
  ASSERT_TRUE(span.auto_reference().valid());

  // We have two different indexed_span instances: indexed_span{key_one} and indexed_span{key_two}.
  ASSERT_NE(indexed.loc(), span.auto_reference().loc());
  ASSERT_EQ(indexed.number(), key_one);
  ASSERT_EQ(span.auto_reference().number(), key_two);

  // Set the field that is used in the reference key to the `key_one` value.
  span.modify().number(key_one);

  // This caused the reference to be recomputed, and now the reference points to indexed_span{key_one}, while the
  // indexed_span{key_two} instance has been free'd.
  ASSERT_EQ(index.indexed_span.size(), 1);

  // Those two are the same indexed_span instance (indexed_span{key_one}).
  ASSERT_EQ(indexed.loc(), span.auto_reference().loc());

  // Release the handle.
  indexed.put();

  // The auto-reference is keeping the span allocated.
  ASSERT_EQ(index.indexed_span.size(), 1);
}

TEST(RenderTest, CachedReference)
{
  static constexpr u32 key_one = 11;
  static constexpr u32 key_two = 22;

  test::app1::Index index;

  auto indexed = index.indexed_span.by_key(key_one);
  ASSERT_TRUE(indexed.valid());
  ASSERT_EQ(indexed.number(), key_one);

  // Only one indexed_span exists for now -- indexed_span{key_one}.
  ASSERT_EQ(index.indexed_span.size(), 1);

  auto span = index.span_with_cached_reference.alloc();
  ASSERT_TRUE(span.valid());

  // Still only one indexed_span exists.
  ASSERT_EQ(index.indexed_span.size(), 1);

  // Assign the field that is used in the reference key (see test.render).
  span.modify().number(key_two);

  // And still only one indexed_span exists.
  ASSERT_EQ(index.indexed_span.size(), 1);

  // Accessing the reference.
  ASSERT_TRUE(span.cached_reference().valid());

  // Accessing the referencing caused the indexed_span{key_two} to be allocated.
  ASSERT_EQ(index.indexed_span.size(), 2);

  // We have two different indexed_span instances: indexed_span{key_one} and indexed_span{key_two}.
  ASSERT_NE(indexed.loc(), span.cached_reference().loc());
  ASSERT_EQ(indexed.number(), key_one);
  ASSERT_EQ(span.cached_reference().number(), key_two);

  // Set the field that is used in the reference key to the `key_one` value.
  span.modify().number(key_one);

  // Still two are allocated -- reference will be recomputed only after it is accessed.
  ASSERT_EQ(index.indexed_span.size(), 2);

  // Access the reference, causing it to be recomputed.
  ASSERT_TRUE(span.cached_reference().valid());

  // We're back to there being only one indexed_span -- indexed_span{key_one}.
  ASSERT_EQ(index.indexed_span.size(), 1);

  // Those are two same indexed_span instances (indexed_span{key_one}).
  ASSERT_EQ(indexed.loc(), span.cached_reference().loc());

  // Release the handle.
  indexed.put();

  // The cached reference is keeping the span allocated.
  ASSERT_EQ(index.indexed_span.size(), 1);
}
