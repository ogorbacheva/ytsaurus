---
metadata:
  - name: generator
    content: Diplodoc Platform v5.5.3
csp:
  - script-src-elem:
      - https://mc.yandex.ru
  - connect-src:
      - https://*.algolia.net
      - https://*.algolianet.com
vcsPath: en/yql/differences_from/clickhouse.md
sourcePath: en/yql/differences_from/clickhouse.md
---
# Differences between ClickHouse and YQL dialects of SQL


## Terminology
* A ClickHouse `ARRAY` is called a `List` in YQL, with the same naming difference applying to most functions used to handle them.
* The ClickHouse `ARRAY JOIN` operation is called [FLATTEN BY](../syntax/flatten.md) in YQL.
* From the YQL perspective, the data type of both physical and nested (Nested in ClickHouse) logical tables is `List<Struct<...>>`. In YQL, [container data types](../types/containers.md) can be combined in any way.

## Syntax
* In ClickHouse, you can assign an alias to almost any part of an expression using `AS` and then reference that alias in another part of the query. In contrast, YQL is very strict about the scope of columns in different parts of a [SELECT](../syntax/select/index.md) statement.
