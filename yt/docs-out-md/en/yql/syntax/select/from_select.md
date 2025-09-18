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
vcsPath: en/yql/syntax/select/from_select.md
sourcePath: en/yql/syntax/select/from_select.md
---
## FROM ... SELECT ... {#from-select}

An inverted format, first specifying the data source and then the operation.

#### Examples

```yql
FROM my_table SELECT key, value;
```

```yql
FROM a_table AS a
JOIN b_table AS b
USING (key)
SELECT *;
```

