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
vcsPath: ru/yql/syntax/select/distinct.md
sourcePath: ru/yql/syntax/select/distinct.md
---
# DISTINCT

Выбор уникальных строк.

{% note info %}

Применение `DISTINCT` к вычислимым значениям на данный момент не реализовано. С этой целью можно использовать подзапрос или выражение [`GROUP BY ... AS ...`](../group_by.md).

{% endnote %}

## Пример

```yql
SELECT DISTINCT value -- только уникальные значения из таблицы
FROM my_table;
```

Также ключевое слово `DISTINCT` может использоваться для применения [агрегатных функций](../../builtins/aggregation.md) только к уникальным значениям. Подробнее см. в документации по [GROUP BY](../group_by.md).
