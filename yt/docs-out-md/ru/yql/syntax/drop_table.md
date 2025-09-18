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
vcsPath: ru/yql/syntax/drop_table.md
sourcePath: ru/yql/syntax/drop_table.md
---
# DROP TABLE

Удаляет указанную таблицу. Таблица по имени ищется в базе данных, заданной оператором [USE](use.md).

Если таблицы с таким именем не существует, возвращается ошибка.

### Примеры

``` yql
DROP TABLE my_table;
```


