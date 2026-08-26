# Async Request in {{product-name}} Flow (Python)

This example shows a two-component [pipeline]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#pipeline) that implements asynchronous request processing. One [computation]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#stream-and-computation) routes events into requests and accumulates responses. The other performs computation without [state]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#state). This is a Python implementation of a similar [C++ example]({{ flow-docs-root }}/{{ lang }}/tutorials/cpp/examples/async_request{{ docs-revision-query }}).

[Source code]({{source-root}}/yt/yt/flow/examples/python/async_request)

## Structure

The pipeline includes two computations:

1. **`state`** (`StateKeeperFunction`) — a stateful computation that:
   - Accepts events from the `event` stream and generates requests to the `request` stream with a unique `request_id`.
   - Accepts responses from the `response` stream and accumulates the total length (`total_length`) in the external [state]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#state).

2. **`processor`** (`RequestProcessorFunction`) — a stateless computation that accepts requests from the `request` stream and immediately returns a response (the length of the request string) to the `response` stream.

The `event → request → response → state` cycle closes between the two computations.

## `state_keeper_function.py`

This file handles routing of incoming streams (`event` / `response`) and working with the external state.

{% code '/yt/yt/flow/examples/python/async_request/state_keeper_function.py' lang='python' lines='[BEGIN state_keeper]-[END state_keeper]' %}

## `request_processor_function.py`

This is a stateless request handler: it calculates the length of the request string and returns the response.

{% code '/yt/yt/flow/examples/python/async_request/request_processor_function.py' lang='python' lines='[BEGIN request_processor]-[END request_processor]' %}

## `__main__.py`

This is the entry point: it creates the pipeline and registers both computations.

{% code '/yt/yt/flow/examples/python/async_request/__main__.py' lang='python' lines='[BEGIN main]-[END main]' %}

## Key patterns

- **Routing by `stream_id`**: the `if stream_id == "event" / "response"` branching lets a single computation handle multiple input streams with different logic.
- **Generating a unique `request_id`**: `random.getrandbits(64)` ensures correlation between the request and the response in the asynchronous cycle.
- **External state** via `ctx.external_state("/state", message)`: the `to_builder()` / `set()` pattern supports cumulative updates to the external state.
- **Stateless computation**: `RequestProcessorFunction` doesn’t use state — it’s a pure transformation of a request into a response, which lets you scale it independently.
- **Two-component pipeline**: `pipeline.add("state", ...)` and `pipeline.add("processor", ...)` register the computations. The streams between them are described in the [spec]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#spec-and-dynamic-spec).

