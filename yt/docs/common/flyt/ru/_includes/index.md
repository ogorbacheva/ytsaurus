# Обзор

FLYT — это проект по интеграции [Apache Flink](https://flink.apache.org/) и {{product-name}}. С его помощью можно использовать Flink для потоковой и пакетной обработки данных, читая их из {{product-name}} и записывая обратно в реальном времени.

## Компоненты {#components}

- [flink-connector-ytsaurus]({{ flyt-docs-root }}/{{ lang }}/reference/flink-connector-ytsaurus{{ docs-revision-query }}) — коннектор Apache Flink к сортированным динамическим таблицам {{product-name}}; поддерживает запись, чтение ограниченных потоков и Lookup-операции;
- [flink-yson]({{ flyt-docs-root }}/{{ lang }}/reference/flink-yson{{ docs-revision-query }}) — форматтер для работы с {% if audience == "public" %}[YSON]({{ core-docs-root }}/{{ lang }}/reference/storage/yson{{ docs-revision-query }}){% else %}[YSON]({{ docs_root }}/ru/core/reference/storage/yson){% endif %} в задачах Flink.

## С чего начать {#getting-started}

Если вы впервые работаете с FLYT, начните с раздела [Быстрый старт]({{ flyt-docs-root }}/{{ lang }}/reference/flink-connector-ytsaurus{{ docs-revision-query }}#quick-start-guide) в документации коннектора.
