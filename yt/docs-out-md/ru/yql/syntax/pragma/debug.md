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
vcsPath: ru/yql/syntax/pragma/debug.md
sourcePath: ru/yql/syntax/pragma/debug.md
---

# Отладочные и служебные {#debug}

## `config.flags("ValidateUdf", "Lazy")`

| Тип значения | По умолчанию |
| --- | --- |
| Строка: None / Lazy / Greedy | None |

Валидация результатов UDF на соответствие объявленной сигнатуре. Greedy режим форсирует материализацию «ленивых» контейнеров, а Lazy — нет.

## `config.flags("Diagnostics")`

| Тип значения | По умолчанию |
| --- | --- |
| Флаг | false |

Получение диагностической информации от YQL в виде дополнительного результата запроса.
