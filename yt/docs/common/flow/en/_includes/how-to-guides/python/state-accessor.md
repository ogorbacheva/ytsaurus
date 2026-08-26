# StateAccessor in {{product-name}} Flow (Python)

Use StateAccessor to read, modify, and delete state values. For general information about stateful processing, see the [Stateful processing]({{ flow-docs-root }}/{{ lang }}/concepts/stateful{{ docs-revision-query }}) section.

## How it works {#how-it-works}

In Flow, the [state]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#state) is stored in [sorted dynamic tables]({{ core-docs-root }}/{{ lang }}/concepts/dynamic-tables/sorted-dynamic-tables{{ docs-revision-query }}). If you’re using [external state]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/external-state{{ docs-revision-query }}), you create this table yourself. If you’re using [internal state]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/internal-state{{ docs-revision-query }}), Flow automatically creates and manages the tables.

The key columns in the state table match the `group_by_schema` of the [computation]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#stream-and-computation) that uses this state. This means the state is tied to the message key — all messages with the same key share one state.

## Reading and writing data {#reading-and-writing-data}

The [worker]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#worker) directly handles table operations (reading, writing, and deleting data). When the worker receives a new batch of messages, it loads the state values for all keys in the batch and sends them to the [companion]({{ flow-docs-root }}/{{ lang }}/concepts/companion{{ docs-revision-query }}) along with the messages and timers. For more details, see the [interaction schema]({{ flow-docs-root }}/{{ lang }}/concepts/companion{{ docs-revision-query }}#schema).

You write new values to the state table transactionally within an [epoch]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#epoch).

## Four accessor types {#accessor-types}

The Python SDK provides four accessor types for working with state:

| Accessor | Format | Retrieval | Description |
|----------|--------|-----------|-------------|
| [YsonStateAccessor]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/internal-state{{ docs-revision-query }}#yson-state-accessor) | YSON (dict) | `ctx.state(name, msg)` | Serializes a Python dict to YSON |
| [RawStateAccessor]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/internal-state{{ docs-revision-query }}#raw-state-accessor) | `bytes` | `ctx.raw_state(name, msg)` | Raw bytes without serialization |
| [ProtoStateAccessor]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/internal-state{{ docs-revision-query }}#proto-state-accessor) | Protobuf | `ctx.proto_state(name, msg, ProtoClass)` | Serializes using Protobuf |
| [ExternalStateAccessor]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/external-state{{ docs-revision-query }}) | Payload (table row) | `ctx.external_state("/name", msg)` | Typed access to an external table |

The first three accessors work with [internal state]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/internal-state{{ docs-revision-query }}) (tables are automatically managed by Flow). `ExternalStateAccessor` works with [external state]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/external-state{{ docs-revision-query }}) (you create the table yourself).

## Common API {#common-api}

All internal accessors (`YsonStateAccessor`, `RawStateAccessor`, `ProtoStateAccessor`) provide the same set of methods:

| Method | Description |
|--------|-------------|
| `get()` | Get the state value (or `None` if the state doesn’t exist) |
| `set(value)` | Set the state value |
| `clear()` | Delete the state for the current key |
| `get_or_default(default)` | Get the value or return `default` |

## Getting an accessor {#getting-accessor}

You get an accessor through `RuntimeContext` (`ctx`) inside `on_message` or `on_timer`:

```python
class MyFunction(RowFunction):
    def on_message(self, message, output, ctx):
        # YSON (dict)
        yson_state = ctx.state("state-name", message)

        # Raw bytes
        raw_state = ctx.raw_state("state-name", message)

        # Protobuf
        proto_state = ctx.proto_state("state-name", message, MyProtoClass)

        # External (the name must start with "/")
        ext_state = ctx.external_state("/state-name", message)

    def on_timer(self, timer, output, ctx):
        # Similarly, but pass timer instead of message
        state = ctx.state("state-name", timer)
```

Parameters:
- `name` — a string with the state name declared in the [static spec]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#spec-and-dynamic-spec). For internal states, this is an arbitrary string from `internal_states`; for external states, it’s a key from `external_state_managers` that must start with `/`.
- `message` / `timer` — the message or timer for which you need to get the state for the [key]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#key).
- `ProtoClass` (only for `proto_state`) — the Protobuf message class for deserialization.

## When to use which type {#choosing-type}

| Situation | Recommended accessor |
|-----------|----------------------|
| A simple dictionary with several fields | `ctx.state()` (YSON) |
| Arbitrary binary data | `ctx.raw_state()` (Raw) |
| Structured data with a fixed schema | `ctx.proto_state()` (Protobuf) |
| Data that needs to be accessed from other systems | `ctx.external_state("/name", msg)` (External) |
| Data that requires a custom table | `ctx.external_state("/name", msg)` (External) |

## See also

- [Internal State (Python)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/internal-state{{ docs-revision-query }})
- [External State (Python)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/external-state{{ docs-revision-query }})
- [Working with states (Python)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/state{{ docs-revision-query }}) — a brief overview of all types
- [Stateful processing (concept)]({{ flow-docs-root }}/{{ lang }}/concepts/stateful{{ docs-revision-query }})
- [StateAccessor (Java)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/java/state-accessor{{ docs-revision-query }})