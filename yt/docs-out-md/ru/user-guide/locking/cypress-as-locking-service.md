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
vcsPath: ru/user-guide/locking/cypress-as-locking-service.md
sourcePath: ru/user-guide/locking/cypress-as-locking-service.md
---
# Распределённые блокировки

{% include [Распределённые блокировки](../../_includes/user-guide/locking/cypress-as-locking-service-p1-de81fc16a3a0.md) %}

## Общие сведения

Для использования YTsaurus в качестве сервиса распределённых блокировок необходимо развернуть мастер-серверы кластера в нескольких локациях.

Локаций должно быть не менее трёх, в этом случае сервис переживёт отключение одной локации. Если мастер-серверы будут расположены в пяти локациях, то сервис переживёт отключение любых двух.

{% include [Распределённые блокировки](../../_includes/user-guide/locking/cypress-as-locking-service-p2-b09689ed0fb4.md) %}

## Квотирование

{% include [Распределённые блокировки](../../_includes/user-guide/locking/cypress-as-locking-service-p3-e469264b7551.md) %}
