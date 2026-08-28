# Overview

FLYT is a project for integrating [Apache Flink](https://flink.apache.org/) with {{product-name}}. It lets you use Flink for streaming and batch data processing, reading data from {{product-name}} and writing results back in real time.

## Components {#components}

- [flink-connector-ytsaurus]({{ flyt-docs-root }}/{{ lang }}/reference/flink-connector-ytsaurus{{ docs-revision-query }}) — Apache Flink connector for sorted dynamic tables in {{product-name}}; supports writes, reading bounded streams, and Lookup operations;
- [flink-yson]({{ flyt-docs-root }}/{{ lang }}/reference/flink-yson{{ docs-revision-query }}) — formatter for working with {% if audience == "public" %}[YSON]({{ core-docs-root }}/{{ lang }}/reference/storage/yson{{ docs-revision-query }}){% else %}[YSON]({{ docs_root }}/ru/core/reference/storage/yson){% endif %} in Flink jobs.

## Getting Started {#getting-started}

If you are new to FLYT, start with the [Quick Start]({{ flyt-docs-root }}/{{ lang }}/reference/flink-connector-ytsaurus{{ docs-revision-query }}#quick-start-guide) section in the connector documentation.
