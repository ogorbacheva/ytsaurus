## См. также

- [Быстрый старт (C++)]({{ flow-docs-root }}/{{ lang }}/tutorials/cpp/quick-start{{ docs-revision-query }})
- [Таймеры]({{ flow-docs-root }}/{{ lang }}/concepts/timers{{ docs-revision-query }})
- [Stateful processing]({{ flow-docs-root }}/{{ lang }}/concepts/stateful{{ docs-revision-query }})

Все эти объекты необходимо создать в {{product-name}}, перед тем как [запускать работу пайплайна]({{ flow-docs-root }}/{{ lang }}/how-to-guides/deployment/update-pipeline{{ docs-revision-query }}#launch-flow).{% if audience == "internal" %} Для создания объектов можно воспользоваться библиотекой [YtSync]({{yt-sync-docs}}/). Она позволяет лаконично описать объекты и их отличия между разными [окружениями]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#environment) и выполнять операции создания, обновления, [миграции]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#migration) (в ряде случаев).{% endif %}

{% if audience == "internal" %}В данном примере продемонстрировано использование режима easy mode, подробную документацию по которому можно найти [здесь]({{yt-sync-docs}}/stages_specification).{% endif %}

Исходники примера с использованием `YtSync` лежат в [tools/yt_sync]({{source-root}}/yt/yt/flow/examples/cpp/wait_click_join/tools/yt_sync):
- [queues.py]({{source-root}}/yt/yt/flow/examples/cpp/wait_click_join/tools/yt_sync/queues.py) &mdash; описание очередей, консьюмеров и продюсеров.
- [tables.py]({{source-root}}/yt/yt/flow/examples/cpp/wait_click_join/tools/yt_sync/tables.py) &mdash; описание таблицы профилей `state`.
- [pipelines.py]({{source-root}}/yt/yt/flow/examples/cpp/wait_click_join/tools/yt_sync/pipelines.py) &mdash; описание пайплайна.
- [stages.py]({{source-root}}/yt/yt/flow/examples/cpp/wait_click_join/tools/yt_sync/stages.py) &mdash; глобальные настройки для окружений.
- [\_\_main\_\_.py]({{source-root}}/yt/yt/flow/examples/cpp/wait_click_join/tools/yt_sync/__main__.py) &mdash; main программы, сводящийся к вызову одной библиотечной функции.
