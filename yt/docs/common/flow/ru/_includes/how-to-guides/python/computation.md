# Computation в {{product-name}} Flow (Python)

{% note info %}

На этой странице описаны Python-специфичные детали работы с компьютейшенами. Общие концепции описаны в разделе [Computation]({{ flow-docs-root }}/{{ lang }}/concepts/computation{{ docs-revision-query }}).

{% endnote %}

## Типы Computation {#computation-types}

Во Flow есть два вида `Computation`: [`Swift`]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#swift) и `Transform`. От их выбора зависит способ обеспечения exactly-once гарантий и то, какие преобразования возможно реализовать с их применением.

| Тип | Способ обеспечения гарантий | Применение |
|-----|-----------------------------|------------|
| `Swift`| Код преобразования детерминирован, при необходимости будет вызываться повторно | Stateless преобразования |
| `Transform` | Результат работы обязательно сохраняется в YT, поэтому нет требований на какую-либо детерминированность преобразований | Stateful преобразования [Подробнее]({{ flow-docs-root }}/{{ lang }}/concepts/stateful{{ docs-revision-query }}) |

При использовании [компаньона]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#companion) выбор `Swift` или `Transform` осуществляется через указание `computation_class_name` в статической [спеке]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#spec-and-dynamic-spec):
- `NYT::NFlow::NCompanion::TTransformCompanionComputation` — для `Transform`.
- `NYT::NFlow::NCompanion::TSwiftMapCompanionComputation` — для `Swift`.
- `NYT::NFlow::NCompanion::TTransformOrderedSourceCompanionComputation` — для `Transform`-сорса.
- `NYT::NFlow::NCompanion::TSwiftOrderedSourceCompanionComputation` — для `Swift`-сорса.

## Создание Computation {#computation}

В Python компьютейшен создаётся через `Pipeline.add()` и регистрируется в `PipelineContext` автоматически. Пример из [WordCount]({{ flow-docs-root }}/{{ lang }}/tutorials/python/examples/wordcount{{ docs-revision-query }}):

{% code '/yt/yt/flow/examples/python/word_count/__main__.py' lang='python' lines='[BEGIN main]-[END main]' %}

{% note warning %}

`process_function=None` недопустим: компьютейшены без бизнес-логики в Python не регистрируют. Если нужен [passthrough]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#passthrough) — не регистрируйте компьютейшен в Python вовсе, а в статической спеке укажите C++-класс passthrough в `computation_class_name` (см. [Passthrough Computation]({{ flow-docs-root }}/{{ lang }}/concepts/computation{{ docs-revision-query }}#passthrough)).

{% endnote %}

В статической спеке создаётся Computation с таким же `id` (в данном примере `mapper`):
```yson
"mapper" = {
    "computation_class_name" = "NYT::NFlow::NCompanion::TTransformCompanionComputation";
    "group_by_schema" = [
        ...
    ];
    "input_stream_ids" = [...];
    "output_stream_ids" = [...];
    "required_resource_ids" = {
        "CompanionManager" = {
            "worker" = true;
            "controller" = false;
        };
    };
    "parameters" = {
        ...
    };
};
```

Подробнее про спеку в разделе [Spec, DynamicSpec и Config]({{ flow-docs-root }}/{{ lang }}/concepts/spec{{ docs-revision-query }}).

## SourceComputation {#sourcecomputation}

`SourceComputation` — вершина в графе [пайплайна]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#pipeline), осуществляющая чтение данных из внешних источников. На стороне воркера ей соответствует [TSwiftOrderedSourceComputation]({{ flow-docs-root }}/{{ lang }}/concepts/computation{{ docs-revision-query }}#tswiftorderedsourcecomputation) или [TTransformOrderedSourceComputation]({{ flow-docs-root }}/{{ lang }}/concepts/computation{{ docs-revision-query }}#ttransformorderedsourcecomputation).

В Python `SourceComputation` создаётся через передачу `source=True` в `Pipeline.add()`. Фильтрация [сообщений]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#message) выполняется внутри Process Function через флаг [distribute]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/distribute{{ docs-revision-query }}).

В статической спеке для детерминированной обработки без пользовательского стейта указывают `TSwiftOrderedSourceCompanionComputation`. Если SourceComputation использует внутренний стейт или недетерминированную логику, указывают `TTransformOrderedSourceCompanionComputation`: воркер материализует выход и фиксирует его вместе со стейтом и смещением источника. Ключ внутреннего стейта в таком компьютейшене — ключ партиции источника.

### Параметры

| Параметр | Обязательный | Описание |
|----------|:---:|----------|
| `computation_id` | Да | Уникальный идентификатор |
| `fn` (process function) | Да | Функция обработки сообщений |

### Создание SourceComputation

```python
pipeline.add("reader", MyParsingFunction(), source=True)
```

Для passthrough Source не используйте Python — укажите в спеке `NYT::NFlow::TSwiftPassthroughOrderedSourceComputation` в `computation_class_name` и оставьте компьютейшен незарегистрированным в Python-компаньоне. Подробнее — [Passthrough Computation]({{ flow-docs-root }}/{{ lang }}/concepts/computation{{ docs-revision-query }}#passthrough).

### Взаимодействие с Worker {#companion-info}

При инициализации [Worker]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#worker) запрашивает у Python-компаньона информацию о зарегистрированных объектах `Computation` и `SourceComputation`. Source-компьютейшен на стороне воркера отправляет входные сообщения в Python-компаньон, который применяет к ним `ProcessFunction` и возвращает результат.

## Process Function {#process-function}

Бизнес-логика обработки данных реализуется через Process Function. Для реализации необходимо выбрать один из двух базовых классов: [RowFunction]({{source-root}}/yt/yt/flow/library/python/companion/computation.py) или [BatchFunction]({{source-root}}/yt/yt/flow/library/python/companion/computation.py).

{% note info %}

Использование `RowFunction` или `BatchFunction` — исключительно вопрос бизнес-логики. `RowFunction` не добавляет накладных расходов на обработку данных относительно использования `BatchFunction` благодаря тому, что Flow внутри себя осуществляет передачу данных батчами.

{% endnote %}

### RowFunction {#row-function}

`RowFunction` получает [сообщения]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#message) и [таймеры]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#timer) по одному. Класс предоставляет два метода:

- `on_message(message, output, ctx)` — вызывается для каждого входного сообщения.
- `on_timer(timer, output, ctx)` — вызывается при срабатывании таймера (опционально).

#### Пример stateless-функции

```python
from yt.yt.flow.library.python.companion.computation import RowFunction


class X2Mapper(RowFunction):
    def on_message(self, message, output, ctx):
        builder = ctx.message_builder("x2_numbers")        # 1
        number = message.payload["number"]                  # 2
        builder.set("number_x2", number * 2)                # 3
        output.add_message(builder.finish())                # 4
```

Разберём построчно:

1. `ctx.message_builder("x2_numbers")` — создаётся `MessageBuilder` для output-[стрима]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#stream-and-computation) с id = `x2_numbers`. Стрим с таким идентификатором должен присутствовать в списке `output_stream_ids` в статической [спеке]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#spec-and-dynamic-spec) компьютейшена.

2. `message.payload["number"]` — получаем значение поля `number` из входящего сообщения. Payload поддерживает dict-like доступ к полям.

3. `builder.set("number_x2", number * 2)` — записываем значение в поле `number_x2`. Это поле должно присутствовать в схеме стрима `x2_numbers` в статической спеке.

4. `output.add_message(builder.finish())` — метод `finish()` возвращает готовое сообщение и сбрасывает билдер для повторного использования. Сообщение добавляется в `OutputCollector`.

### BatchFunction {#batch-function}

`BatchFunction` получает весь список сообщений и таймеров, пришедших от [воркера]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#worker). Класс предоставляет два метода:

- `on_messages(messages, output, ctx)` — вызывается для батча сообщений.
- `on_timers(timers, output, ctx)` — вызывается для батча таймеров (опционально).

#### Пример batch-функции

```python
from yt.yt.flow.library.python.companion.computation import BatchFunction


class X2BatchMapper(BatchFunction):
    def on_messages(self, messages, output, ctx):
        builder = ctx.message_builder("x2_numbers")         # 1
        for message in messages:                             # 2
            number = message.payload["number"]               # 3
            builder.set("number_x2", number * 2)             # 4
            output.add_message(builder.finish())             # 5
```

Ключевое отличие от `RowFunction`:

- `MessageBuilder` создаётся один раз за весь батч (строка 1).
- Метод `finish()` одновременно с возвратом готового сообщения сбрасывает `MessageBuilder` в исходное состояние, после чего его можно переиспользовать для следующего сообщения (строка 5).

## Фильтрация сообщений {#message-filtering}

Для фильтрации сообщений в source-компьютейшенах используется per-message-флаг [distribute]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/distribute{{ docs-revision-query }}): сообщение эмитится из Process Function с `distribute=False` и не публикуется дальше по графу, но учитывается при оценке [watermark]({{ flow-docs-root }}/{{ lang }}/concepts/watermarks{{ docs-revision-query }}).

## Регистрация в Pipeline {#pipeline-registration}

Все компьютейшены регистрируются через `Pipeline.add()`. Внутри `Pipeline` использует `PipelineContext` для хранения и управления зарегистрированными объектами.

```python
from yt.yt.flow.library.python.companion import Pipeline

pipeline = Pipeline()

# Transform-компьютейшен
pipeline.add("computation_id", my_function)

# Source-компьютейшен
pipeline.add("reader", my_function, source=True)
```

Также доступен декоратор `@pipeline.computation`:

```python
pipeline = Pipeline()

@pipeline.computation("mapper")
def mapper(message, output, ctx):
    word = message.payload["word"]
    state = ctx.state("word-state", message)
    data = state.get_or_default({"word": word, "count": 0})
    data["count"] += 1
    state.set(data)
```

{% note warning %}

Каждый Computation должен иметь уникальный идентификатор, соответствующий идентификаторам в статической спеке. Попытка зарегистрировать Computation с уже существующим идентификатором приведёт к ошибке и невозможности старта компаньона.

{% endnote %}

## RuntimeContext {#runtime-context}

[Исходный код]({{source-root}}/yt/yt/flow/library/python/companion/context.py)

`RuntimeContext` (`ctx`) предоставляет доступ к контексту выполнения компьютейшена. Основные методы:

| Метод | Описание |
| --- | --- |
| `ctx.message_builder(stream_id)` | Создать `MessageBuilder` для указанного output-[стрима]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#stream-and-computation) |
| `ctx.parameters` | Словарь параметров компьютейшена из спеки |
| `ctx.min_watermark` | Минимальный [вотермарк]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#timestamps-and-watermarks) по всем входным стримам |
| `ctx.watermark(stream_id)` | [Вотермарк]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#timestamps-and-watermarks) конкретного стрима (`int` или `None`) |
| `ctx.state(name, message)` | Получить YSON-[стейт]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#state), привязанный к ключу сообщения |
| `ctx.raw_state(name, message)` | Получить стейт в виде сырых байтов |
| `ctx.proto_state(name, message, ProtoClass)` | Получить Protobuf-стейт |
| `ctx.external_state(name, message)` | Получить внешний стейт |

Подробнее про работу со стейтами — в разделе [Работа со стейтами (Python)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/state{{ docs-revision-query }}).

### MessageBuilder {#message-builder}

Для создания выходных сообщений используется `MessageBuilder`:

```python
builder = ctx.message_builder("stream_id")
builder.set("field_name", value)
message = builder.finish()
output.add_message(message)
```

Метод `finish()` возвращает готовый объект `Message` и сбрасывает билдер для повторного использования. Поле `stream_id` должно присутствовать в списке `output_stream_ids` в статической [спеке]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#spec-and-dynamic-spec) компьютейшена.

### Параметры компьютейшена {#parameters}

```python
wait_for_actions = ctx.parameters["wait_for_actions"]
```

Словарь `ctx.parameters` содержит параметры, указанные в статической спеке компьютейшена.

### Вотермарки {#watermarks}

```python
# Минимальный вотермарк по всем входным стримам
min_wm = ctx.min_watermark

# Вотермарк конкретного стрима (int или None)
stream_wm = ctx.watermark("stream_id")
```

## OutputCollector {#output-collector}

[Исходный код]({{source-root}}/yt/yt/flow/library/python/companion/computation.py)

`OutputCollector` используется для отправки результатов обработки:

| Метод | Описание |
| --- | --- |
| `output.add_message(message)` | Добавить выходное сообщение (объект `Message`, полученный через `builder.finish()`) |
| `output.add_timer(trigger_timestamp, event_timestamp, stream_id)` | Добавить [таймер]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#timer) с указанным временем срабатывания |
| `output.set_parent_ids(parent_ids)` | Задать parent ID для отслеживания [lineage]({{ flow-docs-root }}/{{ lang }}/concepts/lineage{{ docs-revision-query }}) сообщений. Возвращает новый `OutputCollector` |

Пример создания выходного сообщения и таймера:

```python
def on_message(self, message, output, ctx):
    # Создание выходного сообщения
    builder = ctx.message_builder("output_stream")
    builder.set("field", value)
    output.add_message(builder.finish())

    # Создание таймера
    output.add_timer(trigger_timestamp=1000, event_timestamp=500)
```

## ExtendedMessage {#extended-message}

Входящее [сообщение]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#message) (`ExtendedMessage`) содержит:
- `message.payload` — Payload с dict-like доступом к полям: `message.payload["field"]`.
- `message.stream_id` — идентификатор входного [стрима]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#stream-and-computation) (`str`).
- `message.key` — [ключ]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#key) сообщения (Payload) из `group_by_schema`: `message.key["field"]`.
- `message.event_timestamp` — event timestamp сообщения (`int`).

## Timer {#timer}

Объект [таймера]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#timer) (`Timer`) содержит:
- `timer.key` — [ключ]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#key) таймера (Payload): `timer.key["field"]`.
- `timer.stream_id` — идентификатор стрима таймера (`str`).
- `timer.trigger_timestamp` — время срабатывания (`int`).
- `timer.event_timestamp` — event timestamp (`int`).

## Конфигурация ресурса CompanionManager {#companion-manager}

Для запуска Python-компаньона необходимо объявить ресурс `CompanionManager` в статической спеке:

```yson
"CompanionManager" = {
    "resource_class_name" = "NYT::NFlow::NCompanion::TCompanionManager";
    "parameters" = {
        "entrypoint" = {
            "executable" = "./py_companion";
        };
    };
    "dependencies" = {};
};
```

Параметр `resource_class_name` указывает на класс ресурса, который будет осуществлять запуск компаньона.
В случае Python-компаньона `resource_class_name` всегда должен быть `NYT::NFlow::NCompanion::TCompanionManager`.

Процесс компаньона описывается параметром `entrypoint` (`executable`, `args`, `env`); воркер сам запускает компаньон и следит за его жизненным циклом. При [запуске пайплайна с хоста]({{ flow-docs-root }}/{{ lang }}/tutorials/python/quick-start{{ docs-revision-query }}#launch) через `pipeline.run()` заполнять `entrypoint` вручную не нужно: Python-бинарь сам прописывает `entrypoint = {"executable" = "./py_companion"}`, а `flow_server` доставляет бинарь в джобу под этим именем.

Ключевое отличие от Java-конфигурации: для Java существует отдельный класс ресурса `NYT::NFlow::NCompanion::TJavaCompanionManager` с параметрами `jdk_bin_path`, `classpath` и `main_class`, тогда как Python-компаньон использует общий `TCompanionManager` с параметром `entrypoint`.

Подробнее про спеку в разделе [Spec, DynamicSpec и Config]({{ flow-docs-root }}/{{ lang }}/concepts/spec{{ docs-revision-query }}).

## См. также

- [Computation (концепция)]({{ flow-docs-root }}/{{ lang }}/concepts/computation{{ docs-revision-query }})
- [Работа со стейтами (Python)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/state{{ docs-revision-query }})
- [Быстрый старт (Python)]({{ flow-docs-root }}/{{ lang }}/tutorials/python/quick-start{{ docs-revision-query }})
- [Companion]({{ flow-docs-root }}/{{ lang }}/concepts/companion{{ docs-revision-query }})
