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
vcsPath: en/yql/syntax/into_result.md
sourcePath: en/yql/syntax/into_result.md
---

# INTO RESULT

Lets you specify the user label for [SELECT](select/index.md), [PROCESS](process.md), or [REDUCE](reduce.md). It can't be used along with [DISCARD](discard.md).

## Examples

```yql
SELECT 1 INTO RESULT foo;
```

```yql
SELECT * FROM
my_table
WHERE value % 2 == 0
INTO RESULT `Name of the result`.
```


