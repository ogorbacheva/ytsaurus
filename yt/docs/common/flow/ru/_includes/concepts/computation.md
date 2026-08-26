# Computation в {{product-name}} Flow

Computation — основной строительный блок [пайплайна]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#pipeline). Каждый Computation получает [сообщения]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#message) из входных [потоков]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#stream-and-computation), обрабатывает их и отправляет результаты в выходные потоки.

## Виды Computation {#computation-types}

Во Flow реализовано четыре базовых вида Computation, каждый из которых описан в следующих разделах.

Классы с `Swift` в названии реализуют принцип [Swift]({{ flow-docs-root }}/{{ lang }}/concepts/swift{{ docs-revision-query }}) — подход к обработке данных без полной материализации, но с сохранением [exactly-once]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#exactly-once) гарантий и требованием к детерминированности преобразований.

### TTransformComputation {#ttransformcomputation}
Предназначен для произвольных преобразований входных данных. Результат обработки сохраняется в {{product-name}}, поэтому нет требований к детерминированности. Поддерживает [таймеры]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#timer), [стейты]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#state) и [Sink]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#sink). Для passthrough-варианта без бизнес-логики используют [TPassthroughComputation](#passthrough).

### TSwiftMapComputation {#tswiftmapcomputation}
Реализует детерминированный Map без материализации результатов в {{product-name}}. Не поддерживает таймеры, [Source]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#source) и Sink. Функция преобразования должна быть строго детерминированной — при необходимости результат будет вычислен повторно. Для passthrough-варианта используют [TSwiftPassthroughComputation](#passthrough).

### TSwiftOrderedSourceComputation {#tswiftorderedsourcecomputation}
Основной класс для чтения данных из внешних источников. Требует, чтобы поток данных из каждого инстанса был упорядочен. Поддерживает `WatermarkStrategy` для оценки [вотермарков]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#timestamps-and-watermarks). Для passthrough-варианта используют [TSwiftPassthroughOrderedSourceComputation](#passthrough).

### TTransformOrderedSourceComputation {#ttransformorderedsourcecomputation}

Класс для обработки данных `Source` произвольной пользовательской логикой (парсинг, фильтрация, разворачивание одного сообщения в несколько): пользователь переопределяет `DoProcessMessage`/`DoProcess`, как у `TTransformComputation`, вместо связки `TSwiftPassthroughOrderedSourceComputation` → `TTransformComputation`.

Результат обработки материализуется в {{product-name}}, как у `TTransformComputation`, поэтому требований к детерминированности нет: после рестарта Flow доставляет уже материализованные сообщения с ранее назначенными им `MessageId`, а не вычисляет их заново. Смещение источника, материализованные выходные сообщения и [стейты]({{ flow-docs-root }}/{{ lang }}/concepts/stateful{{ docs-revision-query }}) коммитятся в одной транзакции {{product-name}} — обработка каждого сообщения источника применяется ровно один раз, включая обновления стейта.

Собственный стейт пользователь заводит сам, ровно как в `TTransformComputation`: поле `TMutableStateKeyClient<T>`, инициализация `initContext->InitClient(...)` в `DoInit(IJobInitContextPtr)` и обращение `GetState(message->Key)` при обработке (пример — в разделе [Computation (C++)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/cpp/computation{{ docs-revision-query }}#ttransformorderedsourcecomputation)).

Поддерживаются `source_streams` (ровно один упорядоченный `Source`), несколько выходных стримов, `watermark_strategy` (`watermark_generator` оценивает вотермарки источника, `watermark_alignment` выравнивает чтение, `event_timestamp_assigner` назначает `event_timestamp`), `skip_if_expression` и сообщения с `distribute = false`. Непустой `group_by_schema`, `input`-стримы, [таймеры]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#timer) и [key-visitor-стримы]({{ flow-docs-root }}/{{ lang }}/concepts/key_visitor{{ docs-revision-query }}) приводят к ошибке валидации спеки.

## Passthrough Computation {#passthrough}

[Passthrough-компьютейшен]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#passthrough) не содержит пользовательской бизнес-логики: входящие сообщения конвертируются в схему выходного [стрима]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#stream) и передаются дальше без изменений. Используется для простого приведения схем между стримами, например при чтении очереди и перекладывании данных в другой стрим без какой-либо обработки.

В Flow реализованы три C++-класса:

| Класс | Базовый класс | Назначение |
|-------|--------------|------------|
| `TPassthroughComputation` | `TTransformComputation` | Конвертирует `input`-сообщения в схему `output`-стрима |
| `TSwiftPassthroughComputation` | `TSwiftMapComputation` | Аналогично, без материализации ([Swift]({{ flow-docs-root }}/{{ lang }}/concepts/swift{{ docs-revision-query }})) |
| `TSwiftPassthroughOrderedSourceComputation` | `TSwiftOrderedSourceComputation` | Конвертирует `source`-сообщения в `output`-стрим |

Passthrough реализуется в Flow нативно на C++ и не требует Java- или Python-компаньона. Чтобы включить его, в статической спеке компьютейшена укажите соответствующий C++-класс в поле `computation_class_name`:

```yson
"passthrough" = {
    "computation_class_name" = "NYT::NFlow::TPassthroughComputation";
    "group_by_schema" = [...];
    "input_stream_ids" = [...];
    "output_stream_ids" = [...];
};
```

Подробнее — [Computation (C++)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/cpp/computation{{ docs-revision-query }}#tpassthroughcomputation).

## Общие свойства {#common-properties}
- Всё выполнение в рамках одной [партиции]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#partition) строго однопоточно. Многопоточность достигается за счёт увеличения числа партиций.
- Все Computation берут на себя заполнение метаполей message и timer.
- Объект `OutputCollector` предназначен для сбора выходных сообщений и таймеров.
- Метод `SetParents` позволяет управлять [lineage]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#lineage) сообщений для корректного расчёта метаполей.

## Реализация на разных языках

Каждый язык предоставляет свой набор интерфейсов для реализации Computation:

- **C++**: наследование от базовых классов (`TTransformComputation`, `TSwiftMapComputation` и т.д.) с переопределением методов `DoProcessMessage`/`DoProcessTimer`. [Подробнее →]({{ flow-docs-root }}/{{ lang }}/how-to-guides/cpp/computation{{ docs-revision-query }})
- **Java**: реализация интерфейсов `RowFunction` или `BatchFunction` с методами `onMessage`/`onTimer`. [Подробнее →]({{ flow-docs-root }}/{{ lang }}/how-to-guides/java/computation{{ docs-revision-query }})
- **Python**: наследование от `RowFunction` или `BatchFunction` с методами `on_message`/`on_timer`. [Подробнее →]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/computation{{ docs-revision-query }})
- **Go**: реализация интерфейсов `flow.RowFunction` (`OnMessage`) или `flow.BatchFunction` (`OnMessages`); таймеры — отдельными интерфейсами `flow.RowTimerFunction`/`flow.BatchTimerFunction`. [Подробнее →]({{ flow-docs-root }}/{{ lang }}/how-to-guides/go/computation{{ docs-revision-query }})
- **YQL**: компьютейшны генерируются автоматически по декларативному описанию. [Подробнее →]({{ flow-docs-root }}/{{ lang }}/tutorials/yql/quick-start{{ docs-revision-query }})

## См. также

- [Stateful processing]({{ flow-docs-root }}/{{ lang }}/concepts/stateful{{ docs-revision-query }})
- [Вотермарки]({{ flow-docs-root }}/{{ lang }}/concepts/watermarks{{ docs-revision-query }})
- [Таймеры]({{ flow-docs-root }}/{{ lang }}/concepts/timers{{ docs-revision-query }})
- [Спеки]({{ flow-docs-root }}/{{ lang }}/concepts/spec{{ docs-revision-query }})
- [Computation (C++)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/cpp/computation{{ docs-revision-query }})
- [Computation (Java)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/java/computation{{ docs-revision-query }})
- [Computation (Python)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/computation{{ docs-revision-query }})
- [Computation (Go)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/go/computation{{ docs-revision-query }})
- [Computation (YQL)]({{ flow-docs-root }}/{{ lang }}/reference/yql/features{{ docs-revision-query }})
