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
vcsPath: ru/user-guide/data-processing/spyt/overview.md
sourcePath: ru/user-guide/data-processing/spyt/overview.md
---
{% include [Обзор](../../../_includes/user-guide/data-processing/spyt/overview-p1-af5453c6c298.md) %}

## Что такое SPYT? { #what-is-spyt }

SPYT powered by Apache Spark позволяет запускать Spark-кластер на вычислительных мощностях YTsaurus. Кластер запускается в [Vanilla-операции YTsaurus](../../../user-guide/data-processing/operations/vanilla.md), затем забирает некоторое количество ресурсов из квоты и занимает их постоянно. Spark может читать как [статические](../../storage/static-tables.md), так и [динамические таблицы YTsaurus](../../dynamic-tables/overview.md), делать на них расчеты и писать результат в статическую таблицу.

{% include [Обзор](../../../_includes/user-guide/data-processing/spyt/overview-p2-ca50db8c9d32.md) %}
