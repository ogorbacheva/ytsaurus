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
vcsPath: en/user-guide/data-processing/spyt/overview.md
sourcePath: en/user-guide/data-processing/spyt/overview.md
---
{% include [Overview](../../../_includes/user-guide/data-processing/spyt/overview-p1-ab23b45caa0f.md) %}

## What is SPYT? { #what-is-spyt }

SPYT powered by Apache Spark enables a Spark cluster to be started with YTsaurus computational capacity.  The cluster is started in a [YTsaurus Vanilla operation](../../../user-guide/data-processing/operations/vanilla.md), then takes a certain amount of resources from the quota and occupies them constantly.  Spark can read [static](../../../user-guide/storage/static-tables.md), as well as [dynamic YTsaurus tables](../../dynamic-tables/overview.md), perform computations on them, and record the result in the static table.

{% include [Overview](../../../_includes/user-guide/data-processing/spyt/overview-p2-95df6ac5e162.md) %}
