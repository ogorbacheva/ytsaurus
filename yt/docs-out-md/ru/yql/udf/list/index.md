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
vcsPath: ru/yql/udf/list/index.md
sourcePath: ru/yql/udf/list/index.md
---
# Функции встроенных C++ библиотек

Многие прикладные функции, которые с одной стороны слишком специфичны, чтобы стать частью ядра YQL, а с другой — могут быть полезны широкому кругу пользователей, доступны через встроенные C++ библиотеки.

Для подключения встроенных C++ библиотек используется механизм С++ UDF.

## Список функций

* [Compress Decompress](compress)
* [DateTime](datetime.md)
* [Digest](digest.md)
* [Histogram](histogram.md)
* [Hyperscan](hyperscan.md)
* [Ip](ip.md)
* [Math](math.md)
* [Pcre](pcre.md)
* [Pire](pire.md)
* [Protobuf](protobuf.md)
* [Re2](re2.md)
* [String](string.md)
* [Unicode](unicode.md)
* [Url](url.md)
* [Yson](yson.md)

Будут добавлены в ближайшее время:
* ClickHouse
* PostgreSQL

