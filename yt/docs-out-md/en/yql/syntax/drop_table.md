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
vcsPath: en/yql/syntax/drop_table.md
sourcePath: en/yql/syntax/drop_table.md
---
# DROP TABLE

Deletes the specified table.  Search for the table by name in the database specified by the [USE](use.md) operator.

If there is no such table, an error is returned.

### Examples

```yql
DROP TABLE my_table;
```


