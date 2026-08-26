# StateAccessor в {{product-name}} Flow (Python)

StateAccessor — интерфейс для чтения, модификации и удаления значений [стейта]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#state).
Общие сведения о stateful-обработке описаны в разделе [Stateful processing]({{ flow-docs-root }}/{{ lang }}/concepts/stateful{{ docs-revision-query }}).

## Принцип работы {#how-it-works}

[Стейт]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#state) во Flow хранится в [сортированных динамических таблицах]({{ core-docs-root }}/{{ lang }}/concepts/dynamic-tables/sorted-dynamic-tables{{ docs-revision-query }}).
В случае [внешнего стейта]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/external-state{{ docs-revision-query }}) эта таблица создаётся пользователем, в случае [внутреннего стейта]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/internal-state{{ docs-revision-query }}) таблицы создаются и управляются Flow автоматически.

Для `TTransformCompanionComputation` ключевые колонки в таблице стейта совпадают с `group_by_schema` [компьютейшена]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#stream-and-computation). Для внутреннего стейта `TTransformOrderedSourceCompanionComputation` ключом служит ключ партиции источника: `group_by_schema` в таком SourceComputation не поддерживается. Во всех случаях сообщения с одинаковым ключом разделяют один стейт.

## Чтение и запись данных {#reading-and-writing-data}

Непосредственную работу с таблицей (чтение, запись, удаление данных) осуществляет [воркер]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#worker). При получении очередного батча сообщений воркер загружает значения стейтов для всех ключей в батче и отправляет их в [компаньон]({{ flow-docs-root }}/{{ lang }}/concepts/companion{{ docs-revision-query }}) вместе с сообщениями и таймерами. Подробнее про [схему взаимодействия]({{ flow-docs-root }}/{{ lang }}/concepts/companion{{ docs-revision-query }}#schema).

Запись новых значений в таблицу стейта осуществляется транзакционно в рамках [эпохи]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#epoch).

## Четыре типа аксессоров {#accessor-types}

Python SDK предоставляет четыре вида аксессоров для работы со стейтом:

| Аксессор | Формат | Получение | Описание |
|----------|--------|-----------|----------|
| [YsonStateAccessor]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/internal-state{{ docs-revision-query }}#yson-state-accessor) | YSON (dict) | `ctx.state(name, msg)` | Сериализация Python dict в YSON |
| [RawStateAccessor]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/internal-state{{ docs-revision-query }}#raw-state-accessor) | `bytes` | `ctx.raw_state(name, msg)` | Сырые байты без сериализации |
| [ProtoStateAccessor]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/internal-state{{ docs-revision-query }}#proto-state-accessor) | Protobuf | `ctx.proto_state(name, msg, ProtoClass)` | Сериализация через Protobuf |
| [ExternalStateAccessor]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/external-state{{ docs-revision-query }}) | Payload (строка таблицы) | `ctx.external_state("/name", msg)` | Типизированный доступ к внешней таблице |

Первые три аксессора работают с [внутренним стейтом]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/internal-state{{ docs-revision-query }}) (таблицы управляются Flow автоматически). `ExternalStateAccessor` работает с [внешним стейтом]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/external-state{{ docs-revision-query }}) (пользователь создаёт таблицу самостоятельно).

## Общий API {#common-api}

Все внутренние аксессоры (`YsonStateAccessor`, `RawStateAccessor`, `ProtoStateAccessor`) предоставляют единый набор методов:

| Метод | Описание |
|-------|----------|
| `get()` | Получить значение стейта (или `None`, если стейт отсутствует) |
| `set(value)` | Установить значение стейта |
| `clear()` | Удалить стейт для текущего ключа |
| `get_or_default(default)` | Получить значение или вернуть `default` |

## Получение аксессора {#getting-accessor}

Аксессор получается через `RuntimeContext` (`ctx`) внутри `on_message` или `on_timer`:

```python
class MyFunction(RowFunction):
    def on_message(self, message, output, ctx):
        # YSON (dict)
        yson_state = ctx.state("state-name", message)

        # Raw bytes
        raw_state = ctx.raw_state("state-name", message)

        # Protobuf
        proto_state = ctx.proto_state("state-name", message, MyProtoClass)

        # External (имя обязательно начинается с "/")
        ext_state = ctx.external_state("/state-name", message)

    def on_timer(self, timer, output, ctx):
        # Аналогично, но вместо message передаётся timer
        state = ctx.state("state-name", timer)
```

Параметры:
- `name` — строка с именем стейта, объявленным в [статической спеке]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#spec-and-dynamic-spec). Для внутренних стейтов — произвольная строка из `internal_states`; для внешних стейтов — ключ из `external_state_managers`, обязательно начинающийся с `/`.
- `message` / `timer` — сообщение или таймер, для [ключа]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#key) которого нужно получить стейт.
- `ProtoClass` (только для `proto_state`) — класс Protobuf-сообщения для десериализации.

## Когда использовать какой тип {#choosing-type}

| Ситуация | Рекомендуемый аксессор |
|----------|----------------------|
| Простой словарь с несколькими полями | `ctx.state()` (YSON) |
| Произвольные двоичные данные | `ctx.raw_state()` (Raw) |
| Структурированные данные с фиксированной схемой | `ctx.proto_state()` (Protobuf) |
| Данные, к которым нужен доступ из других систем | `ctx.external_state("/name", msg)` (External) |
| Данные, требующие пользовательской таблицы | `ctx.external_state("/name", msg)` (External) |

## См. также

- [Internal State (Python)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/internal-state{{ docs-revision-query }})
- [External State (Python)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/external-state{{ docs-revision-query }})
- [Работа со стейтами (Python)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/state{{ docs-revision-query }}) — краткий обзор всех типов
- [Stateful processing (концепция)]({{ flow-docs-root }}/{{ lang }}/concepts/stateful{{ docs-revision-query }})
- [StateAccessor (Java)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/java/state-accessor{{ docs-revision-query }})
