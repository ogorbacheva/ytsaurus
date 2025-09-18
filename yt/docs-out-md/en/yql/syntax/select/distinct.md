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
vcsPath: en/yql/syntax/select/distinct.md
sourcePath: en/yql/syntax/select/distinct.md
---
# DISTINCT

Selecting unique rows.

{% note info %}

Applying `DISTINCT` to calculated values is not currently implemented. For this purpose, use a subquery or the clause [`GROUP BY ... AS ...`](../group_by.md).

{% endnote %}

## Example

```yql
SELECT DISTINCT value -- only unique values from the table
FROM my_table;
```

The `DISTINCT` keyword can also be used to apply [aggregate functions](../../builtins/aggregation.md) only to distinct values. For more information, see the documentation for [GROUP BY](../group_by.md).

