## See also

- [Quick start (C++)]({{ flow-docs-root }}/{{ lang }}/tutorials/cpp/quick-start{{ docs-revision-query }})
- [Timers]({{ flow-docs-root }}/{{ lang }}/concepts/timers{{ docs-revision-query }})
- [Stateful processing]({{ flow-docs-root }}/{{ lang }}/concepts/stateful{{ docs-revision-query }})

You must create all these objects in {{product-name}} before you [start the pipeline]({{ flow-docs-root }}/{{ lang }}/how-to-guides/deployment/basic-rules{{ docs-revision-query }}#launch-flow).{% if audience == "internal" %} You can use the [YtSync]({{yt-sync-docs}}/) library to create the objects. It lets you concisely describe the objects and their differences across various [environments]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#environment) and perform create, update, and [migration]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#migration) operations (in some cases).{% endif %}

{% if audience == "internal" %}This example demonstrates the use of easy mode. You can find detailed documentation on it [here]({{yt-sync-docs}}/stages_specification).{% endif %}

{% if audience == "internal" %}The example source code that uses `YtSync` is in [tools/yt_sync]({{source-root}}/yt/yt/flow/examples/cpp/wait_click_join/tools/yt_sync):

- [queues.py]({{source-root}}/yt/yt/flow/examples/cpp/wait_click_join/tools/yt_sync/queues.py) — description of queues, consumers, and producers.
- [tables.py]({{source-root}}/yt/yt/flow/examples/cpp/wait_click_join/tools/yt_sync/tables.py) — table specifications. The example declares none (`TABLES = {}`); this is where you would describe any dynamic tables of your own.
- [pipelines.py]({{source-root}}/yt/yt/flow/examples/cpp/wait_click_join/tools/yt_sync/pipelines.py) — description of the pipeline.
- [stages.py]({{source-root}}/yt/yt/flow/examples/cpp/wait_click_join/tools/yt_sync/stages.py) — global settings for environments.
- [__main__.py]({{source-root}}/yt/yt/flow/examples/cpp/wait_click_join/tools/yt_sync/__main__.py) — the program’s main entry point, which boils down to calling a single library function.{% endif %}