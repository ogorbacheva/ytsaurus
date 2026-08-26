# Message processing order in {{product-name}} Flow

In a distributed streaming system, it’s not practical to ensure a strict global order for all messages: data comes from multiple sources and is processed in parallel across different [partitions]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#partition) and [computations]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#computation). Flow addresses this differently, using the `AlignmentTimestamp` mechanism.

Each message carries an [AlignmentTimestamp](#alignment-timestamp)—a monotonically increasing timestamp that defines the processing priority:

- **Within a single [stream]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#stream)**: messages are processed in the order `(AlignmentTimestamp, message_id)`—this gives a fully deterministic order.
- **Across different [streams]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#stream)**: a merging priority queue is used based on [StabilizedEventTimestamp](#stabilized-event-timestamp) (calculated from `AlignmentTimestamp`) with an adjustment for [stream_delays]({{ flow-docs-root }}/{{ lang }}/concepts/spec{{ docs-revision-query }}#inputordering); if values are equal, ordering uses `TaskId`.

In addition to prioritization, there’s an order guarantee for **derived messages**—see the [Order guarantees](#ordering-guarantees) section for details.

## AlignmentTimestamp {#alignment-timestamp}

Along with [SystemTimestamp and EventTimestamp]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#timestamps-and-watermarks), each message in Flow includes the `AlignmentTimestamp` field—a timestamp used to align processing progress across streams and partitions.

Rules for setting `AlignmentTimestamp`:

- **Messages from source streams**: equal to `WriteTimestamp`, which is the time when the message was written to the persistent queue (for example, in QYT{% if audience == "internal" %} or Logbroker{% endif %}).
- **Output messages from `TransformComputation`**: equal to `SystemTimestamp` at the time the message is created.
- **In all other cases**: inherited unchanged from parent messages.

## StabilizedEventTimestamp {#stabilized-event-timestamp}

`StabilizedEventTimestamp` is a computed timestamp equal to `AlignmentTimestamp` + `bias`, where `bias` is the average difference between `EventTimestamp` and `AlignmentTimestamp` within a stream. `bias` is calculated based on the messages in the stream that are currently being processed. It’s used as a non-decreasing substitute for `EventTimestamp` to prioritize the processing of messages from different streams.

## Order guarantees {#ordering-guarantees}

Message order in a stream isn’t guaranteed in general, but there is a specific guarantee for derived messages.

If, in the input stream, message A precedes message B, and they have derived messages A' and B' such that:

- their [lineage]({{ flow-docs-root }}/{{ lang }}/concepts/lineage{{ docs-revision-query }}) keys match (meaning the [grouping keys]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#key) are the same in all intermediate computations along the path from the source to the current computation),
- and in the current computation, A' and B' share the same key,

then A' will be processed before B'.

## Partition prioritization within a single stream {#partition-prioritization}

Within a single source stream, partitions are prioritized by the `WriteTimestamp` of their messages: partitions with earlier messages are processed first. This prioritization is implemented through the processing order of output messages in subsequent computations.

## Cross-stream prioritization {#cross-stream-prioritization}

Across different streams, prioritization is based on `EventTimestamp`, but indirectly: ordering actually happens by `AlignmentTimestamp` with a stream-specific adjustment.

The adjustment is calculated as the average difference between `EventTimestamp` and `AlignmentTimestamp` among the messages currently in all output buffers for that stream:

$$ordering_priority = AlignmentTimestamp + avg(EventTimestamp − AlignmentTimestamp)$$

This approach accounts for each stream’s specifics (for example, the systematic delay between the time a message is written to the queue and the event time) and ensures a fairer cross-stream ordering.

## See also

- [Computation]({{ flow-docs-root }}/{{ lang }}/concepts/computation{{ docs-revision-query }})
- [Watermarks]({{ flow-docs-root }}/{{ lang }}/concepts/watermarks{{ docs-revision-query }})
- [Timers]({{ flow-docs-root }}/{{ lang }}/concepts/timers{{ docs-revision-query }})