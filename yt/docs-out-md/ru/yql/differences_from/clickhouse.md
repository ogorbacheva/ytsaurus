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
vcsPath: ru/yql/differences_from/clickhouse.md
sourcePath: ru/yql/differences_from/clickhouse.md
---
# Отличия в SQL диалектах ClickHouse и YQL


## Терминология
* `ARRAY` из ClickHouse называется `List` в YQL, соответствующим образом отличается большинство работающих с ними функций.
* `ARRAY JOIN` из ClickHouse в YQL называется [FLATTEN BY](../syntax/flatten.md).
* С точки зрения YQL, тип данных логической таблицы — `List<Struct<...>>`, как физической, так и вложенной (Nested из ClickHouse). В YQL можно комбинировать [контейнерные типы данных](../types/containers.md) произвольным образом.

## Синтаксис
* В ClickHouse можно дать практически любой части выражения имя через `AS` и затем использовать его в другой части запроса, а в YQL всё очень строго с областью видимости колонок в различных частях [SELECT](../syntax/select/index.md).
