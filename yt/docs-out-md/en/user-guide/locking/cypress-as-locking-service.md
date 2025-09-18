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
vcsPath: en/user-guide/locking/cypress-as-locking-service.md
sourcePath: en/user-guide/locking/cypress-as-locking-service.md
---
# Distributed locks

{% include [Distributed locks](../../_includes/user-guide/locking/cypress-as-locking-service-p1-be5f13b208d7.md) %}

## General information

To use YTsaurus as a distributed locking service, you need to deploy master servers of a cluster in multiple locations.

There must be at least three locations so that the service can work if one of them is not available. If the master servers are located in five locations, the service can work if any two of them are not available.

{% include [Distributed locks](../../_includes/user-guide/locking/cypress-as-locking-service-p2-eded927f13a1.md) %}

## Quotas

{% include [Distributed locks](../../_includes/user-guide/locking/cypress-as-locking-service-p3-adb9b8e1a984.md) %}
