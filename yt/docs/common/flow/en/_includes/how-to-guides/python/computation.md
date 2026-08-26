# Computation in {{product-name}} Flow (Python)

{% note info %}

This page describes Python-specific details for working with computations. For general concepts, see the [Computation]({{ flow-docs-root }}/{{ lang }}/concepts/computation{{ docs-revision-query }}) section.

{% endnote %}

## Computation Types {#computation-types}

In Flow, there are two types of `Computation`: [`Swift`]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#swift) and `Transform`. Your choice determines how exactly-once guarantees are provided and what transformations you can implement.

| Type | Guarantee Approach | Use Case |
|------|--------------------|----------|
| `Swift` | The transformation code is deterministic and will be called again if needed | Stateless transformations |
| `Transform` | The result is always stored in YT, so no determinism requirements apply to the transformations | Stateful transformations [Learn more]({{ flow-docs-root }}/{{ lang }}/concepts/stateful{{ docs-revision-query }}) |

When using a [companion]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#companion), you select `Swift` or `Transform` by specifying `computation_class_name` in the static [spec]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#spec-and-dynamic-spec):
- `NYT::NFlow::NCompanion::TTransformCompanionComputation` — for `Transform`.
- `NYT::NFlow::NCompanion::TSwiftMapCompanionComputation` — for `Swift`.

## Creating a Computation {#computation}

In Python, you create a computation with `Pipeline.add()`, and it registers automatically in `PipelineContext`. See an example from [WordCount]({{ flow-docs-root }}/{{ lang }}/tutorials/python/examples/wordcount{{ docs-revision-query }}):

{% code '/yt/yt/flow/examples/python/word_count/__main__.py' lang='python' lines='[BEGIN main]-[END main]' %}

{% note warning %}

`process_function=None` is not allowed: computations without business logic aren’t registered in Python. If you need [passthrough]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#passthrough), don’t register the computation in Python at all. Instead, specify the C++ passthrough class in `computation_class_name` in the static spec (see [Passthrough Computation]({{ flow-docs-root }}/{{ lang }}/concepts/computation{{ docs-revision-query }}#passthrough)).

{% endnote %}

In the static spec, you create a Computation with the same `id` (in this example, `mapper`):
```yson
"mapper" = {
    "computation_class_name" = "NYT::NFlow::NCompanion::TTransformCompanionComputation";
    "group_by_schema" = [
        ...
    ];
    "input_stream_ids" = [...];
    "output_stream_ids" = [...];
    "required_resource_ids" = {
        "CompanionManager" = {
            "worker" = true;
            "controller" = false;
        };
    };
    "parameters" = {
        ...
    };
};
```

For more on specs, see the [Spec, DynamicSpec, and Config]({{ flow-docs-root }}/{{ lang }}/concepts/spec{{ docs-revision-query }}) section.

## SourceComputation {#sourcecomputation}

`SourceComputation` is the top node in the [pipeline]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#pipeline) graph that reads data from external sources. Learn more about [Source Computation]({{ flow-docs-root }}/{{ lang }}/concepts/computation{{ docs-revision-query }}#tswiftorderedsourcecomputation).

In Python, you create a `SourceComputation` by passing `source=True` to `Pipeline.add()`. You filter [messages]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#message) inside the Process Function using the [distribute]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/distribute{{ docs-revision-query }}) flag.

### Parameters

| Parameter | Required | Description |
|-----------|----------|-------------|
| `computation_id` | Yes | Unique identifier |
| `fn` (process function) | Yes | Function for processing messages |

### Creating a SourceComputation

```python
pipeline.add("reader", MyParsingFunction(), source=True)
```

For a passthrough Source, don’t use Python. Instead, specify `NYT::NFlow::TSwiftPassthroughOrderedSourceComputation` in `computation_class_name` in the spec, and leave the computation unregistered in the Python companion. See [Passthrough Computation]({{ flow-docs-root }}/{{ lang }}/concepts/computation{{ docs-revision-query }}#passthrough) for more details.

### Interaction with Worker {#companion-info}

When initializing, the [Worker]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#worker) requests information about registered `Computation` and `SourceComputation` objects from the Python companion. Each input message from `TSwiftOrderedSourceCompanionComputation` is sent to the Python companion, which applies the `ProcessFunction` and returns the result. The Worker makes one request to the companion per message.

## Process Function {#process-function}

You implement the business logic for data processing in a Process Function. To do this, choose one of two base classes: [RowFunction]({{source-root}}/yt/yt/flow/library/python/companion/computation.py) or [BatchFunction]({{source-root}}/yt/yt/flow/library/python/companion/computation.py).

{% note info %}

Choosing between `RowFunction` and `BatchFunction` depends entirely on your business logic. `RowFunction` doesn’t add extra processing overhead compared to `BatchFunction` because Flow internally transfers data in batches.

{% endnote %}

### RowFunction {#row-function}

`RowFunction` receives [messages]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#message) and [timers]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#timer) one at a time. The class provides two methods:

- `on_message(message, output, ctx)` — called for each input message.
- `on_timer(timer, output, ctx)` — called when a timer fires (optional).

#### Example of a stateless function

```python
from yt.yt.flow.library.python.companion.computation import RowFunction


class X2Mapper(RowFunction):
    def on_message(self, message, output, ctx):
        builder = ctx.message_builder("x2_numbers")        # 1
        number = message.payload["number"]                  # 2
        builder.set("number_x2", number * 2)                # 3
        output.add_message(builder.finish())                # 4
```

Let’s go line by line:

1. `ctx.message_builder("x2_numbers")` — creates a `MessageBuilder` for the output [stream]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#stream-and-computation) with id = `x2_numbers`. A stream with this identifier must be present in the `output_stream_ids` list in the static [spec]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#spec-and-dynamic-spec) of the computation.
2. `message.payload["number"]` — gets the value of the `number` field from the incoming message. The payload supports dict-like access to fields.
3. `builder.set("number_x2", number * 2)` — writes the value to the `number_x2` field. This field must exist in the schema of the `x2_numbers` stream in the static spec.
4. `output.add_message(builder.finish())` — the `finish()` method returns the completed message and resets the builder for reuse. The message is added to the `OutputCollector`.

### BatchFunction {#batch-function}

`BatchFunction` receives the entire list of messages and timers that came from the [worker]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#worker). The class provides two methods:

- `on_messages(messages, output, ctx)` — called for a batch of messages.
- `on_timers(timers, output, ctx)` — called for a batch of timers (optional).

#### Example of a batch function

```python
from yt.yt.flow.library.python.companion.computation import BatchFunction


class X2BatchMapper(BatchFunction):
    def on_messages(self, messages, output, ctx):
        builder = ctx.message_builder("x2_numbers")         # 1
        for message in messages:                             # 2
            number = message.payload["number"]               # 3
            builder.set("number_x2", number * 2)             # 4
            output.add_message(builder.finish())             # 5
```

The key difference from `RowFunction` is:

- You create the `MessageBuilder` once for the entire batch (line 1).
- The `finish()` method returns the completed message and resets the `MessageBuilder` to its initial state, so you can reuse it for the next message (line 5).

## Message Filtering {#message-filtering}

To filter messages in source computations, use the per-message [distribute]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/distribute{{ docs-revision-query }}) flag: you emit the message from the Process Function with `distribute=False`, and it isn’t published further along the graph, but it’s still considered when evaluating the [watermark]({{ flow-docs-root }}/{{ lang }}/concepts/watermarks{{ docs-revision-query }}).

## Registration in Pipeline {#pipeline-registration}

You register all computations with `Pipeline.add()`. Inside, `Pipeline` uses `PipelineContext` to store and manage registered objects.

```python
from yt.yt.flow.library.python.companion import Pipeline

pipeline = Pipeline()

# Transform computation
pipeline.add("computation_id", my_function)

# Source computation
pipeline.add("reader", my_function, source=True)
```

You can also use the `@pipeline.computation` decorator:

```python
pipeline = Pipeline()

@pipeline.computation("mapper")
def mapper(message, output, ctx):
    word = message.payload["word"]
    state = ctx.state("word-state", message)
    data = state.get_or_default({"word": word, "count": 0})
    data["count"] += 1
    state.set(data)
```

{% note warning %}

Each Computation must have a unique identifier that matches the identifiers in the static spec. Trying to register a Computation with an existing identifier will cause an error and prevent the companion from starting.

{% endnote %}

## RuntimeContext {#runtime-context}

[Source code]({{source-root}}/yt/yt/flow/library/python/companion/context.py)

`RuntimeContext` (`ctx`) gives you access to the computation’s runtime context. Key methods:

| Method | Description |
|--------|-------------|
| `ctx.message_builder(stream_id)` | Create a `MessageBuilder` for the specified output [stream]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#stream-and-computation) |
| `ctx.parameters` | Map of the computation’s parameters from the spec |
| `ctx.min_watermark` | Minimum [watermark]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#timestamps-and-watermarks) across all input streams |
| `ctx.watermark(stream_id)` | [Watermark]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#timestamps-and-watermarks) of a specific stream (`int` or `None`) |
| `ctx.state(name, message)` | Get the YSON [state]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#state) tied to the message key |
| `ctx.raw_state(name, message)` | Get the state as raw bytes |
| `ctx.proto_state(name, message, ProtoClass)` | Get the Protobuf state |
| `ctx.external_state(name, message)` | Get the external state |

For more on working with states, see the [Working with States (Python)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/state{{ docs-revision-query }}) section.

### MessageBuilder {#message-builder}

You use `MessageBuilder` to create output messages:

```python
builder = ctx.message_builder("stream_id")
builder.set("field_name", value)
message = builder.finish()
output.add_message(message)
```

The `finish()` method returns the completed `Message` object and resets the builder for reuse. The `stream_id` field must be present in the `output_stream_ids` list in the static [spec]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#spec-and-dynamic-spec) of the computation.

### Computation Parameters {#parameters}

```python
wait_for_actions = ctx.parameters["wait_for_actions"]
```

The `ctx.parameters` map contains the parameters specified in the static spec of the computation.

### Watermarks {#watermarks}

```python
# Minimum watermark across all input streams
min_wm = ctx.min_watermark

# Watermark of a specific stream (int or None)
stream_wm = ctx.watermark("stream_id")
```

## OutputCollector {#output-collector}

[Source code]({{source-root}}/yt/yt/flow/library/python/companion/computation.py)

Use `OutputCollector` to send processing results:

| Method | Description |
| --- | --- |
| `output.add_message(message)` | Add an output message (a `Message` object obtained via `builder.finish()`) |
| `output.add_timer(trigger_timestamp, event_timestamp, stream_id)` | Add a [timer]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#timer) with the specified trigger time |
| `output.set_parent_ids(parent_ids)` | Set parent IDs to track the [lineage]({{ flow-docs-root }}/{{ lang }}/concepts/lineage{{ docs-revision-query }}) of messages. Returns a new `OutputCollector` |

Example of creating an output message and a timer:

```python
def on_message(self, message, output, ctx):
    # Create an output message
    builder = ctx.message_builder("output_stream")
    builder.set("field", value)
    output.add_message(builder.finish())

    # Create a timer
    output.add_timer(trigger_timestamp=1000, event_timestamp=500)
```

## ExtendedMessage {#extended-message}

An incoming [message]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#message) (`ExtendedMessage`) contains:
- `message.payload` — Payload with dict-like access to fields: `message.payload["field"]`.
- `message.stream_id` — The ID of the input [stream]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#stream-and-computation) (`str`).
- `message.key` — The [key]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#key) of the message (Payload) from `group_by_schema`: `message.key["field"]`.
- `message.event_timestamp` — The event timestamp of the message (`int`).

## Timer {#timer}

A [timer]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#timer) object (`Timer`) contains:
- `timer.key` — The [key]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#key) of the timer (Payload): `timer.key["field"]`.
- `timer.stream_id` — The ID of the timer’s stream (`str`).
- `timer.trigger_timestamp` — The trigger time (`int`).
- `timer.event_timestamp` — The event timestamp (`int`).

## CompanionManager resource configuration {#companion-manager}

To run a Python companion, you need to declare the `CompanionManager` resource in a static spec:

```yson
"CompanionManager" = {
    "resource_class_name" = "NYT::NFlow::NCompanion::TCompanionManager";
    "parameters" = {
        "entrypoint" = {
            "executable" = "./py_companion";
        };
    };
    "dependencies" = {};
};
```

The `resource_class_name` parameter specifies the resource class that will launch the companion.
For a Python companion, `resource_class_name` must always be `NYT::NFlow::NCompanion::TCompanionManager`.

The companion process is described by the `entrypoint` parameter (`executable`, `args`, `env`). When you [launch a pipeline from a host]({{ flow-docs-root }}/{{ lang }}/tutorials/python/quick-start{{ docs-revision-query }}#launch) via `pipeline.run()`, you don’t need to fill in `entrypoint` manually: the Python binary sets `entrypoint = {"executable" = "./py_companion"}` for every `TCompanionManager` resource in the spec, and `flow_server` delivers the binary to the job under that name.

The key difference from the Java configuration is that Java has a separate resource class `NYT::NFlow::NCompanion::TJavaCompanionManager` with parameters `jdk_bin_path`, `classpath`, and `main_class`, while the Python companion uses the shared `TCompanionManager` with the `entrypoint` parameter.

For more details about the spec, see the section [Spec, DynamicSpec and Config]({{ flow-docs-root }}/{{ lang }}/concepts/spec{{ docs-revision-query }}).

## See also

- [Computation (concept)]({{ flow-docs-root }}/{{ lang }}/concepts/computation{{ docs-revision-query }})
- [Working with states (Python)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/state{{ docs-revision-query }})
- [Quick start (Python)]({{ flow-docs-root }}/{{ lang }}/tutorials/python/quick-start{{ docs-revision-query }})
- [Companion]({{ flow-docs-root }}/{{ lang }}/concepts/companion{{ docs-revision-query }})
