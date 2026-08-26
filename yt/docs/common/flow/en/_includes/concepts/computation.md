# Computation in {{product-name}} Flow

Use a Computation as the main building block of a [pipeline]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#pipeline). You get messages from input [streams]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#stream-and-computation), process them, and send the results to output streams.

## Computation types {#computation-types}

Flow implements four basic Computation types. You’ll find each type described in the sections below.

Classes with `Swift` in their name follow the [Swift]({{ flow-docs-root }}/{{ lang }}/concepts/swift{{ docs-revision-query }}) principle. This is an approach to data processing without full materialization, while preserving [exactly-once]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#exactly-once) guarantees and requiring deterministic transformations.

### TTransformComputation {#ttransformcomputation}

Use this for arbitrary transformations of input data. The processing result is stored in {{product-name}}, so you don’t need deterministic transformations. It supports [timers]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#timer), [states]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#state), and [Sink]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#sink). For a passthrough variant without business logic, use [TPassthroughComputation](#passthrough).

### TSwiftMapComputation {#tswiftmapcomputation}

This implements a deterministic Map without materializing results in {{product-name}}. It doesn’t support timers, [Source]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#source), or Sink. Your transformation function must be strictly deterministic — the system recomputes the result if needed. For a passthrough variant, use [TSwiftPassthroughComputation](#passthrough).

### TSwiftOrderedSourceComputation {#tswiftorderedsourcecomputation}

This is the main class for reading data from external sources. It requires that the data stream from each instance is ordered. It supports `WatermarkStrategy` to estimate [watermarks]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#timestamps-and-watermarks). For a passthrough variant, use [TSwiftPassthroughOrderedSourceComputation](#passthrough).

### TTransformOrderedSourceComputation {#ttransformorderedsourcecomputation}

This class processes `Source` data with arbitrary custom logic (parsing, filtering, expanding one message into several): you override `DoProcessMessage`/`DoProcess` the same way as in `TTransformComputation`, instead of chaining `TSwiftPassthroughOrderedSourceComputation` → `TTransformComputation`.

The processing result is materialized in {{product-name}}, as in `TTransformComputation`, so there are no determinism requirements: after a restart, Flow delivers the already materialized messages with the `MessageId` values previously assigned to them instead of recomputing them. The source offset, the materialized output messages, and the [states]({{ flow-docs-root }}/{{ lang }}/concepts/stateful{{ docs-revision-query }}) are committed in a single {{product-name}} transaction — processing of each source message is applied exactly once, including state updates.

You declare your own state yourself, exactly as in `TTransformComputation`: a `TMutableStateKeyClient<T>` field, initialization via `initContext->InitClient(...)` in `DoInit(IJobInitContextPtr)`, and a `GetState(message->Key)` call during processing (for an example, see the [Computation (C++)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/cpp/computation{{ docs-revision-query }}#ttransformorderedsourcecomputation) section).

Supported: `source_streams` (exactly one ordered `Source`), several output streams, `watermark_strategy` (`watermark_generator` estimates the source watermarks, `watermark_alignment` aligns reading, `event_timestamp_assigner` assigns `event_timestamp`), `skip_if_expression`, and messages with `distribute = false`. A non-empty `group_by_schema`, `input` streams, [timers]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#timer), and [key-visitor streams]({{ flow-docs-root }}/{{ lang }}/concepts/key_visitor{{ docs-revision-query }}) cause a spec validation error.

## Passthrough Computation {#passthrough}

A [passthrough Computation]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#passthrough) doesn’t include custom business logic. Incoming messages are converted to the output [stream]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#stream) schema and passed on unchanged. Use it to simply align schemas between streams, for example, when reading a queue and moving data to another stream without any processing.

Flow implements three C++ classes:

| Class | Base class | Purpose |
|-------|--------------|------------|
| `TPassthroughComputation` | `TTransformComputation` | Converts `input` messages to the `output` stream schema |
| `TSwiftPassthroughComputation` | `TSwiftMapComputation` | Does the same, without materialization ([Swift]({{ flow-docs-root }}/{{ lang }}/concepts/swift{{ docs-revision-query }})) |
| `TSwiftPassthroughOrderedSourceComputation` | `TSwiftOrderedSourceComputation` | Converts `source` messages to the `output` stream |

Passthrough is natively implemented in Flow using C++ and doesn’t require a Java or Python companion. To enable it, specify the corresponding C++ class in the `computation_class_name` field in the Computation’s static spec:

```yson
"passthrough" = {
    "computation_class_name" = "NYT::NFlow::TPassthroughComputation";
    "group_by_schema" = [...];
    "input_stream_ids" = [...];
    "output_stream_ids" = [...];
};
```

For more details, see [Computation (C++)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/cpp/computation{{ docs-revision-query }}#tpassthroughcomputation).

## Common properties {#common-properties}

- All execution within a single [partition]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#partition) is strictly single-threaded. You achieve multithreading by increasing the number of partitions.
- All Computations handle filling the message and timer metadata fields.
- The `OutputCollector` object collects output messages and timers.
- The `SetParents` method lets you manage the [lineage]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#lineage) of messages to correctly calculate metadata fields.

## Implementation in different languages

Each language offers its own set of interfaces for implementing a Computation:

- **C++**: inherit from base classes (`TTransformComputation`, `TSwiftMapComputation`, etc.) and override the `DoProcessMessage`/`DoProcessTimer` methods. [Learn more →]({{ flow-docs-root }}/{{ lang }}/how-to-guides/cpp/computation{{ docs-revision-query }})
- **Java**: implement the `RowFunction` or `BatchFunction` interfaces with the `onMessage`/`onTimer` methods. [Learn more →]({{ flow-docs-root }}/{{ lang }}/how-to-guides/java/computation{{ docs-revision-query }})
- **Python**: inherit from `RowFunction` or `BatchFunction` and use the `on_message`/`on_timer` methods. [Learn more →]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/computation{{ docs-revision-query }})
- **YQL**: Computations are generated automatically based on a declarative description. [Learn more →]({{ flow-docs-root }}/{{ lang }}/tutorials/yql/quick-start{{ docs-revision-query }})

## See also

- [Stateful processing]({{ flow-docs-root }}/{{ lang }}/concepts/stateful{{ docs-revision-query }})
- [Watermarks]({{ flow-docs-root }}/{{ lang }}/concepts/watermarks{{ docs-revision-query }})
- [Timers]({{ flow-docs-root }}/{{ lang }}/concepts/timers{{ docs-revision-query }})
- [Specs]({{ flow-docs-root }}/{{ lang }}/concepts/spec{{ docs-revision-query }})
- [Computation (C++)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/cpp/computation{{ docs-revision-query }})
- [Computation (Java)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/java/computation{{ docs-revision-query }})
- [Computation (Python)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/computation{{ docs-revision-query }})
- [Computation (YQL)]({{ flow-docs-root }}/{{ lang }}/reference/yql/features{{ docs-revision-query }})