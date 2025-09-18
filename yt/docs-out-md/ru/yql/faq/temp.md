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
vcsPath: ru/yql/faq/temp.md
sourcePath: ru/yql/faq/temp.md
---
# Временные данные

## Директория для временных данных

Через прагму `yt.TmpFolder` пользователь может задавать директорию для размещения временных таблиц и файлов. Такая необходимость возникает в следующих случаях:
* Нет доступа на запись в директорию `//tmp` YTsaurus-кластера.
* Запрос обрабатывает таблицы с чувствительными данными. По умолчанию временные таблицы и файлы сохраняются в `//tmp`, куда есть доступ на чтение у всех пользователей кластера. Чтобы избежать раскрытия чувствительной информации, необходимо явно задавать в запросе директорию для временных данных, закрытую ACL.

## Контроль роста TmpFolder директории

При использовании в запросе прагмы `yt.TmpFolder` в указанной директории постепенно начинают накапливаться данные. В ней создаются следующие поддиректории:

* query_cache &mdash; [кеш запросов](../syntax/pragma#querycache);
* tmp &mdash; временные таблицы, которые существуют только на время работы запроса;
* new_cache &mdash; файловый кеш YTsaurus API:
    * исполняемые файлы YQL;
    * пользовательские и системные UDF;
    * приложенные к запросу файлы;
    * таблицы, используемые в MapJoin, если передаются не родным для YTsaurus способом.

Поддиректория query_cache и время жизни её содержимого управляется прагмами [yt.QueryCacheMode](../syntax/pragma#querycache) и [yt.QueryCacheTtl](../syntax/pragma#ytquerycachettl). Для `yt.QueryCacheMode="disable"` кеш запросов полностью отключается и данная поддиректория не создаётся/не используется. Прагма `yt.QueryCacheTtl` позволяет ограничить время жизни таблиц в кеше запросов.

Временные таблицы запроса из поддиректории tmp очищаются автоматически после завершения запроса. Исключение составляют запросы, в которых задана прагма `yt.ReleaseTempData="never"`, и таблицы, которые отдаются в результатах запросов как Full result. Временем жизни таких таблиц можно управлять прагмой [yt.TempTablesTtl](../syntax/pragma#yttemptablesttl). Расположение временных таблиц можно отдельно переопределить через прагму [yt.TablesTmpFolder](../syntax/pragma#yttablestmpfolder).

Время жизни объектов внутри new_cache управляется прагмой [yt.FileCacheTtl](../syntax/pragma#ytfilecachettl). Из этой директории можно исключить исполняемые файлы YQL и UDF-ки, задав прагму [yt.BinaryTmpFolder](../syntax/pragma#ytbinarytmpfolder) с указанием отдельной директории и своим TTL (см. [yt.BinaryExpirationInterval](../syntax/pragma#ytbinaryexpirationinterval)). Также можно вынести таблицы, используемые в MapJoin, в отдельную директорию через прагму [yt.TableContentTmpFolder](../syntax/pragma#yt.tablecontenttmpfolder), но при этом стоит помнить, что в данных таблицах могут присутствовать чувствительные данные.

