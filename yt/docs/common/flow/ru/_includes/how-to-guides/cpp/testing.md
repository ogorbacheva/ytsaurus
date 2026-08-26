# Тестирование в {{product-name}} Flow (C++)

{% note info %}

C++ [воркеры]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#worker) на данный момент не имеют отдельного фреймворка для юнит-тестирования компьютейшенов: основной подход — запускать пайплайн целиком с конечными источниками данных. Пример теста можно найти в [examples/cpp/wait_click_join]({{source-root}}/yt/yt/flow/examples/cpp/wait_click_join). Если писать юнит тесты всё же необходимо, то можно выносить бизнес-логику из компьютейшнов в отдельные классы и писать юнит тесты уже на них.

{% endnote %}

{% include notitle [_](../../_partials/testing-integration-body.md) %}

{% include notitle [_](../../_partials/testing-test-param-body.md) %}

## См. также

- [Базовые правила выкатки]({{ flow-docs-root }}/{{ lang }}/how-to-guides/deployment/update-pipeline{{ docs-revision-query }}#release-and-configure-basic-rules)
- [Тестирование (Java)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/java/testing{{ docs-revision-query }})
- [Тестирование (Python)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/testing{{ docs-revision-query }})
- [Быстрый старт (C++)]({{ flow-docs-root }}/{{ lang }}/tutorials/cpp/quick-start{{ docs-revision-query }})
- [Быстрый старт (Java)]({{ flow-docs-root }}/{{ lang }}/tutorials/java/quick-start{{ docs-revision-query }})
- [Быстрый старт (Python)]({{ flow-docs-root }}/{{ lang }}/tutorials/python/quick-start{{ docs-revision-query }})
