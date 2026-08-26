# Getting started with {{product-name}} Flow

This section walks you through the steps to implement and run your own pipeline in Flow.

{% include [Language choice](language-choice.md) %}

## General plan

No matter which language you choose, creating a pipeline involves the following steps:

1. **Try the [Quick start]({{ flow-docs-root }}/{{ lang }}/tutorials/quick-start{{ docs-revision-query }})** — run a minimal NoOp pipeline to get familiar with the Flow infrastructure.

2. **Review the basic concepts**. Read the [glossary]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}) to understand the Flow model: pipelines, streams, computations, and messages.

3. **Study the concepts**. Get to know [Computation]({{ flow-docs-root }}/{{ lang }}/concepts/computation{{ docs-revision-query }}), [Watermarks and Timers]({{ flow-docs-root }}/{{ lang }}/concepts/watermarks{{ docs-revision-query }}), and [Stateful processing]({{ flow-docs-root }}/{{ lang }}/concepts/stateful{{ docs-revision-query }}), as well as the [guarantees]({{ flow-docs-root }}/{{ lang }}/concepts/guarantees{{ docs-revision-query }}) provided by the system.

4. **Explore examples** in your chosen language:
   - C++: [WordCount]({{ flow-docs-root }}/{{ lang }}/tutorials/cpp/examples/word_count{{ docs-revision-query }}), [Shuffle]({{ flow-docs-root }}/{{ lang }}/tutorials/cpp/examples/shuffle{{ docs-revision-query }}), [WaitClickJoin]({{ flow-docs-root }}/{{ lang }}/tutorials/cpp/examples/wait_click_join{{ docs-revision-query }})
   - Java: [WordCount]({{ flow-docs-root }}/{{ lang }}/tutorials/java/examples/wordcount{{ docs-revision-query }}), [Shuffle]({{ flow-docs-root }}/{{ lang }}/tutorials/java/examples/shuffle{{ docs-revision-query }}), [WaitClickJoin]({{ flow-docs-root }}/{{ lang }}/tutorials/java/examples/wait_click_join{{ docs-revision-query }})
   - Python: [WordCount]({{ flow-docs-root }}/{{ lang }}/tutorials/python/examples/wordcount{{ docs-revision-query }}), [Shuffle]({{ flow-docs-root }}/{{ lang }}/tutorials/python/examples/shuffle{{ docs-revision-query }}), [WaitClickJoin]({{ flow-docs-root }}/{{ lang }}/tutorials/python/examples/wait_click_join{{ docs-revision-query }})
   - YQL: [Quick start]({{ flow-docs-root }}/{{ lang }}/tutorials/yql/quick-start{{ docs-revision-query }})

5. **Check out the available [connectors]({{ flow-docs-root }}/{{ lang }}/concepts/connectors/overview{{ docs-revision-query }})** — queues, static tables{% if audience == "internal" %}, Logbroker{% endif %}, and others.

6. **Describe the pipeline spec** in YSON format. In addition to the examples, the [Spec & DynamicSpec]({{ flow-docs-root }}/{{ lang }}/concepts/spec{{ docs-revision-query }}) section will help you.

7. **Implement your business logic** in the language you’ve chosen, following the relevant quick start guide.

8. **Create the necessary objects in {{product-name}}** — tables, queues, and the pipeline{% if audience == "internal" %} — using the [YtSync]({{yt-sync-docs}}/) utility (the pipeline specification is described [here]({{yt-sync-docs}}/pipeline_specification)){% endif %}.{% if audience == "internal" %} If needed, do the same in third-party systems like [Logbroker](../../yandex-specific/flow/extensions/logbroker.md).{% endif %}

9. **Write tests**. Follow the instructions for your programming language:
   - [C++]({{ flow-docs-root }}/{{ lang }}/how-to-guides/cpp/testing{{ docs-revision-query }})
   - [Java]({{ flow-docs-root }}/{{ lang }}/how-to-guides/java/testing{{ docs-revision-query }})
   - [Python]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/testing{{ docs-revision-query }})

10. **Run the pipeline** and monitor it via the {{product-name}} UI. For details on releases, read [Releases and pipeline management]({{ flow-docs-root }}/{{ lang }}/how-to-guides/deployment/basic-rules{{ docs-revision-query }}).

## See also

- [About Flow]({{ flow-docs-root }}/{{ lang }}/concepts/overview{{ docs-revision-query }})
- [Quick start]({{ flow-docs-root }}/{{ lang }}/tutorials/quick-start{{ docs-revision-query }})
- [Basic concepts]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }})
- [Connectors]({{ flow-docs-root }}/{{ lang }}/concepts/connectors/overview{{ docs-revision-query }})
{% if audience == "internal" %}- [Comparison with alternative technologies](../../yandex-specific/flow/other/comparison.md){% endif %}