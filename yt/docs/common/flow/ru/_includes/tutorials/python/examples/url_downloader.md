# URL Downloader в {{product-name}} Flow (Python)

Пример [пайплайна]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#pipeline), группирующего входящие URL по хосту и обрабатывающего их пакетно с помощью [таймеров]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#timer). Показывает типичный паттерн «накопить в стейте → обработать по таймеру → очистить стейт».

[Исходный код]({{source-root}}/yt/yt/flow/examples/python/url_downloader)

## Структура

Пайплайн состоит из одного transform-[компьютейшена]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#stream-and-computation) `url_downloader`, который:
1. Принимает сообщения с полями `host` и `url` из входного [стрима]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#stream-and-computation).
2. Накапливает URL для каждого хоста во внутреннем YSON-[стейте]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#state).
3. Устанавливает таймер на 5 секунд вперёд при каждом новом URL.
4. По срабатыванию таймера обрабатывает все накопленные URL и публикует результаты в выходной стрим `processed_urls`.

## `url_download_function.py`

Основная логика обработки. Функция реализует оба метода `RowFunction`: `on_message` для накопления URL в стейте и `on_timer` для их пакетной обработки.

{% code '/yt/yt/flow/examples/python/url_downloader/url_download_function.py' lang='python' lines='[BEGIN url_download_function]-[END url_download_function]' %}

## `__main__.py`

Точка входа: создание пайплайна и регистрация единственного компьютейшена `url_downloader`.

{% code '/yt/yt/flow/examples/python/url_downloader/__main__.py' lang='python' lines='[BEGIN main]-[END main]' %}

## Ключевые паттерны

- **Таймер-driven пакетная обработка**: `output.add_timer(int(time.time()) + 5)` в `on_message` и логика обработки в `on_timer` — стандартный способ пакетировать события по временному окну.
- **Внутренний YSON-стейт** через `ctx.state("host-state", message)`: паттерн `get_or_default` / `set` / `clear` для накопления и очистки данных.
- **Ключ стейта по хосту**: группировка URL по хосту обеспечивается через `group_by_schema` в [спеке]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#spec-and-dynamic-spec), так что каждый экземпляр компьютейшена обрабатывает URL одного хоста изолированно.
- **MessageBuilder**: `ctx.message_builder("processed_urls")` для построения выходных сообщений с явным указанием схемы стрима.
- **Защитная проверка в `on_timer`**: `if not data or not data.get("pending_urls")` предотвращает повторную обработку уже очищенного стейта.


