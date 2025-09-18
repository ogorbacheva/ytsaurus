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
vcsPath: ru/yql/syntax/create_table.md
sourcePath: ru/yql/syntax/create_table.md
---
# CREATE TABLE


Таблица создается автоматически при первом [INSERT INTO](insert_into.md), в заданной оператором [USE](use.md) базе данных. Схема при этом определяется автоматически.

