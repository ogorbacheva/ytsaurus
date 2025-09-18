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
vcsPath: ru/yql/syntax/select/where.md
sourcePath: ru/yql/syntax/select/where.md
---
# WHERE

Фильтрация строк в результате выполнения `SELECT` по условию в колоночной или строковой таблице.

## Пример

```yql
SELECT key FROM my_table
WHERE value > 0;
```
