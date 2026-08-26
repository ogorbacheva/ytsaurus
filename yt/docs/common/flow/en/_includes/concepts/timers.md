# Timers in {{product-name}} Flow

## Why you need timers {#why-timers}

Many stream processing tasks require waiting. For example, you might need to treat a conversion as failed if no click arrives within 30 minutes after an impression. The standard message exchange between computations isn’t suitable for this: a message is either present or not, and you can’t wait for an event’s “absence”.

Timers solve this problem. A `TransformComputation` can create a timer — tell the system, “Wake me up when time X arrives.” When the moment comes, the computation receives a call to `on_timer` / `onTimer` / `DoProcessTimer` and can make a decision based on the accumulated [state]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#state).

Typical use cases:
- **Join with wait**: correlate an ad impression with a click that might arrive with a delay.
- **Timeout**: handle a situation where the expected event never arrives.
- **Windowed aggregations**: close a time window and output the result when the accumulated data becomes stale.

## How timers work {#how-timers-work}

### Lifecycle {#lifecycle}

1. **Registration**. The computation calls `output.add_timer` / `output.addTimer` / `output->AddTimer`, passing `TriggerTimestamp` and `EventTimestamp`.
2. **Storage**. The timer is reliably stored in {{product-name}} and additionally cached in the process memory.
3. **Triggering**. When the [EventWatermark]({{ flow-docs-root }}/{{ lang }}/concepts/watermarks{{ docs-revision-query }}#event-watermark) (or another configured watermark) exceeds `TriggerTimestamp`, the timer is delivered to the computation.
4. **Processing and deletion**. The processing result and the timer deletion are recorded in a single transaction — this ensures exactly-once guarantees (see [Deduplication]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#deduplication)).

### Timer fields {#timer-fields}

Each timer includes two timestamp fields:

- `TriggerTimestamp` — the moment when the timer should trigger. The time scale (`event_time`, `system_time`, `real_time`) is set in the [configuration](#configuration).
- `EventTimestamp` — the business time of the original event, held in the timer. It lets you know when the triggering event occurred while processing the timer.

The timer is also bound to the **[grouping key]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#key)** (the same key that `Computation` uses), so it automatically lands in the correct partition and stays isolated from other keys.

## Which computations support timers {#supported-computations}

| Computation | Timer support |
|---|---|
| `TTransformComputation` | ✓ |
| `TSwiftMapComputation` | ✗ |
| `TSwiftOrderedSourceComputation` | ✗ |
| `TTransformOrderedSourceComputation` | ✗ |

## Configuration {#configuration}

To enable timers in a computation, you must fill the `timers` field in its spec. Each array element describes a single timer stream with the following parameters:

{% include notitle [_](../../../reference/configuration/NYT_NFlow_TTimerSpec.md) %}

Explanations:

- **`time_type`** defines the scale that the system uses to compare `TriggerTimestamp` with the current watermark:
  - `event_time` (default) — compared with `EventWatermark` across all `input` streams (or the streams listed in `streams`).
  - `system_time` — compared with `SystemWatermark`.
  - `real_time` — compared with real astronomical time.

- `streams` / `streams_with_delays` — let you limit which input streams contribute to the watermark calculation for this timer. `streams_with_delays` also lets you set an individual delay for each stream.

- `deduplicate_equal_timestamps` — when you create multiple timers with the same key and `TriggerTimestamp`, only one is kept (the one with the smallest `EventTimestamp`). This is enabled by default.

## Timer structure {#timer-structure}

{% include notitle [_](../../../reference/configuration/NYT_NFlow_TTimerSerializer.md) %}

## API by language {#api}

### C++ {#api-cpp}

```cpp
// Create a timer in DoProcessMessage:
output->AddTimer(TSystemTimestamp(message.EventTimestamp.Underlying() + TDuration::Minutes(30).Seconds()));

// Process the triggered timer:
void DoProcessTimer(const TTimer& timer, IOutputCollectorPtr output) override {
    // timer.Key, timer.EventTimestamp, timer.TriggerTimestamp
    auto builder = MakeMessageBuilder();
    // ...
    output->AddMessage(builder.Finish());
}
```

For more details, see the [Computation (C++)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/cpp/computation{{ docs-revision-query }}) section.

### Java {#api-java}

```java
// Create a timer in onMessage:
output.addTimer(message.getEventTimestamp() + 30 * 60_000_000_000L, message.getEventTimestamp());

// Process the triggered timer (RowFunction):
@Override
public void onTimer(Timer timer, OutputCollector output, RuntimeContext ctx) {
    // timer.getKey(), timer.getEventTimestamp(), timer.getTriggerTimestamp()
}

// For BatchFunction:
@Override
public void onTimers(List<Timer> timers, OutputCollector output, RuntimeContext ctx) { ... }
```

For more details, see the [Computation (Java)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/java/computation{{ docs-revision-query }}) section.

### Python {#api-python}

```python
# Create a timer in on_message:
output.add_timer(trigger_timestamp=message.event_timestamp + 30 * 60_000_000_000, event_timestamp=message.event_timestamp)

# Process the triggered timer (RowFunction):
def on_timer(self, timer, output, ctx):
    # timer.key, timer.event_timestamp, timer.trigger_timestamp, timer.stream_id

# For BatchFunction:
def on_timers(self, timers, output, ctx):
    for timer in timers:
        ...
```

For more details, see the [Computation (Python)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/computation{{ docs-revision-query }}) section.

### Go {#api-go}

```go
// Create a timer in OnMessage:
out.AddTimer(flow.TimerRequest{
    TriggerTimestamp: msg.EventTimestamp + 30*60_000_000_000,
    EventTimestamp:   msg.EventTimestamp,
})

// Process the triggered timer (RowFunction):
func (f myFunction) OnTimer(
    ctx context.Context,
    rt flow.Runtime,
    timer flow.Timer,
    out flow.OutputCollector,
) error {
    // timer.Key, timer.EventTimestamp, timer.TriggerTimestamp, timer.StreamID
    return nil
}

// For BatchFunction:
func (f myFunction) OnTimers(ctx context.Context, rt flow.Runtime, timers []flow.Timer, out flow.OutputCollector) error { ... }
```

For more details, see the [Computation (Go)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/go/computation{{ docs-revision-query }}) section.

## Examples {#examples}

Example implementation of a join with wait (impression + click, 30-minute timeout):

- [C++]({{ flow-docs-root }}/{{ lang }}/tutorials/cpp/examples/wait_click_join{{ docs-revision-query }})
- [Java]({{ flow-docs-root }}/{{ lang }}/tutorials/java/examples/wait_click_join{{ docs-revision-query }})
- [Python]({{ flow-docs-root }}/{{ lang }}/tutorials/python/examples/wait_click_join{{ docs-revision-query }})
- [Go]({{ flow-docs-root }}/{{ lang }}/tutorials/go/examples/wait_click_join{{ docs-revision-query }})

## Limitations and known issues {#limitations}

{% note warning %}

**Process memory**. In the current implementation, all active timers are additionally stored in the process memory, on top of {{product-name}}. With a large number of timers, this can lead to Out of Memory errors when jobs start.

{% endnote %}

- **`TSwiftMapComputation` doesn’t support timers**. If you need timer functionality, use `TTransformComputation`.
- Timestamps are transmitted in nanoseconds (uint64).

## See also

- [Watermarks and Timers]({{ flow-docs-root }}/{{ lang }}/concepts/watermarks{{ docs-revision-query }})
- [Stateful processing]({{ flow-docs-root }}/{{ lang }}/concepts/stateful{{ docs-revision-query }})
- [Computation (C++)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/cpp/computation{{ docs-revision-query }})
- [Computation (Java)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/java/computation{{ docs-revision-query }})
- [Computation (Python)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/computation{{ docs-revision-query }})
- [Computation (Go)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/go/computation{{ docs-revision-query }})