# Lineage in {{product-name}} Flow

Lineage (from English, “genealogy”) is information about which input [messages]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#message) and [timers]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#timer) a specific output result of a [computation]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#computation) was derived from. You use this information during processing so the framework can compute metadata and ensure ordering guarantees. Note that this information isn’t stored in the output message itself.

## Why you need lineage {#why-lineage}

The framework uses lineage for two purposes:

1. **Computing meta-fields.** Based on parent messages, Flow automatically populates the meta-fields of output messages, such as `EventTimestamp`, `AlignmentTimestamp`, and others. For Swift computations and [passthrough]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#passthrough), `AlignmentTimestamp` is inherited from the parents unchanged. This ensures correct [prioritization]({{ flow-docs-root }}/{{ lang }}/concepts/ordering{{ docs-revision-query }}) of messages in downstream computations.
2. **Guaranteeing the order of derived messages.** If two messages share the same [grouping keys]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#key) across the entire lineage chain from the source to the current computation, their relative processing order is preserved. For more details, see the section [Message Processing Order]({{ flow-docs-root }}/{{ lang }}/concepts/ordering{{ docs-revision-query }}#ordering-guarantees).

## Default behavior {#default-behavior}

In most cases, you don’t need to manage lineage explicitly—the framework automatically sets the parents:

| Function type | Parent of the output message |
| --- | --- |
| `RowFunction` / `DoProcessMessage` | The current input message |
| `BatchFunction` / `DoProcess` | All messages in the current batch |
| Timer handler | The current timer |

## When to set lineage explicitly {#explicit-lineage}

By default, the entire current batch is considered the parent of the output message. Setting lineage explicitly lets you narrow this set to a specific subset of input objects. This makes the computation of `EventTimestamp` and `AlignmentTimestamp` more precise.

## API {#api}

You set lineage using the `SetParents` / `set_parent_ids` / `setParentIds` method on the `OutputCollector` object. The method returns a **new** collector with the lineage context attached. All calls to `AddMessage` / `add_message` / `addMessage` on this collector will carry that lineage.

For more details on how to use this in each language:
- [C++]({{ flow-docs-root }}/{{ lang }}/how-to-guides/cpp/computation{{ docs-revision-query }}#output-collector)
- [Java]({{ flow-docs-root }}/{{ lang }}/how-to-guides/java/computation{{ docs-revision-query }}#output-collector)
- [Python]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/computation{{ docs-revision-query }}#output-collector)

## See also

- [Message Processing Order]({{ flow-docs-root }}/{{ lang }}/concepts/ordering{{ docs-revision-query }})
- [Computation]({{ flow-docs-root }}/{{ lang }}/concepts/computation{{ docs-revision-query }})
- [Core Concepts (Glossary)]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }})