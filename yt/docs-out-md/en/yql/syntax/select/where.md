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
vcsPath: en/yql/syntax/select/where.md
sourcePath: en/yql/syntax/select/where.md
---
# WHERE

Filtering rows in the `SELECT`  result based on a condition in tables.

## Example

```yql
SELECT key FROM my_table
WHERE value > 0;
```

