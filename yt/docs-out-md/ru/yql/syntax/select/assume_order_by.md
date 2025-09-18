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
vcsPath: ru/yql/syntax/select/assume_order_by.md
sourcePath: ru/yql/syntax/select/assume_order_by.md
---

# ASSUME ORDER BY

Проверка сортированности результата `SELECT` по значению в указанном столбце или нескольких столбцах. Результат такого `SELECT`-а будет считаться сортированным, но без выполнения фактической сортировки. Проверка сортированности осуществляется на этапе исполнения запроса.

Как и для `ORDER BY`, поддерживается задание порядка сортировки с помощью ключевых слов `ASC` (по возрастанию) и `DESC` (по убыванию). Выражения в `ASSUME ORDER BY` не поддерживается.

## Примеры

```yql
SELECT key || "suffix" as key, -CAST(subkey as Int32) as subkey
FROM my_table
ASSUME ORDER BY key, subkey DESC;
```
