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
vcsPath: ru/api/cli/install.md
sourcePath: ru/api/cli/install.md
---
{% include [Установка](../../_includes/api/cli/install-p1-042db492c3ad.md) %}

## Установка из PyPI-репозитория

Пакет называется `ytsaurus-client`. Перед установкой пакета можно поставить пакет `wheel`, чтобы иметь возможность поставить версию, отличную от системной, или YTsaurus CLI без sudo.

По умолчанию из PyPI устанавливается последняя стабильная версия пакета.
Все тестовые версии имеют суффикс `a1` и могут быть установлены через pip путем добавления опции `--pre`.
Команда для установки из pypi:
  ```bash
  # Установка YTsaurus CLI
  $ pip install ytsaurus-client
  # Установка YSON-bindings
  $ pip install ytsaurus-yson
  ```

{% include [Автодополнение](../../_includes/api/cli/install-p2-7cfdb099090b.md) %}