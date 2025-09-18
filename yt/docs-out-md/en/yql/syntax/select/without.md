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
vcsPath: en/yql/syntax/select/without.md
sourcePath: en/yql/syntax/select/without.md
---
# WITHOUT

Excluding columns from the result of `SELECT *`. The `IF EXISTS` modifier does not throw an error for missing columns.

## Examples

```yql
SELECT * WITHOUT foo, bar FROM my_table;
SELECT * WITHOUT IF EXISTS foo, bar FROM my_table;
```

```yql
PRAGMA simplecolumns;
SELECT * WITHOUT t.foo FROM my_table AS t
CROSS JOIN (SELECT 1 AS foo) AS v;
```

