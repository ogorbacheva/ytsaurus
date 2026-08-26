# Word Count в {{product-name}} Flow (Python)

Простейший пример stateful-[пайплайна]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#pipeline) на Python: подсчёт количества вхождений каждого слова с использованием внутреннего YSON-[стейта]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#state).

[Исходный код]({{source-root}}/yt/yt/flow/examples/python/word_count)

## Структура

Пайплайн состоит из одного transform-[компьютейшена]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#stream-and-computation) `mapper`, который читает слова из входного [стрима]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#stream-and-computation) и обновляет счётчик в стейте.

## `__main__.py`

Точка входа: создание пайплайна и регистрация единственного компьютейшена.

{% code '/yt/yt/flow/examples/python/word_count/__main__.py' lang='python' lines='[BEGIN main]-[END main]' %}

## `word_count_mapper.py`

`RowFunction`, использующая `ctx.state()` для работы с YSON-стейтом. Для каждого ключа сообщения хранится словарь с полями `word` и `count`.

{% code '/yt/yt/flow/examples/python/word_count/word_count_mapper.py' lang='python' lines='[BEGIN word_count_mapper]-[END word_count_mapper]' %}

## Ключевые паттерны

- Простейший stateful-пайплайн с одним компьютейшеном.
- Внутренний YSON-стейт через `ctx.state()` с `get_or_default` / `set`.
- Ключ стейта определяется `group_by_schema` из [спеки]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#spec-and-dynamic-spec) (в данном случае -- по полю `word`).


