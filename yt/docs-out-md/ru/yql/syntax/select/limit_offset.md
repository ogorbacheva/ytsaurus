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
vcsPath: ru/yql/syntax/select/limit_offset.md
sourcePath: ru/yql/syntax/select/limit_offset.md
---

# LIMIT и OFFSET

`LIMIT` ограничивает вывод указанным количеством строк. Если значение лимита равно `NULL`, или `LIMIT` не указан, то вывод не ограничен.

`OFFSET` указывает отступ от начала (в строках). Если значение отступа равно `NULL`, или `OFFSET` не указан, то используется значение ноль.

## Примеры

```yql
SELECT key FROM my_table
LIMIT 7;
```

```yql
SELECT key FROM my_table
LIMIT 7 OFFSET 3;
```

```yql
SELECT key FROM my_table
LIMIT 3, 7; -- эквивалентно предыдущему примеру
```

```yql
SELECT key FROM my_table
LIMIT NULL OFFSET NULL; -- эквивалентно SELECT key FROM my_table
```
