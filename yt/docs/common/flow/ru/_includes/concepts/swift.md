# Swift в {{product-name}} Flow

**Swift** — принцип обработки данных в {{product-name}} Flow, при котором результат работы [компьютейшена]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#computation) **не сохраняется в {{product-name}}**. Вместо этого функция преобразования должна быть строго детерминированной: если результат потребуется повторно (например, при перезапуске джоба), он будет вычислен заново из тех же входных данных.

{% if audience == "internal" %}

Принцип зародился в [BigRT](https://docs.yandex-team.ru/big_rt/) и был перенесён в Flow.

{% endif %}

## Зачем это нужно {#motivation}

В классическом подходе (`TTransformComputation`) каждая [эпоха]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#epoch) завершается транзакционной записью результатов в {{product-name}}. Это обеспечивает exactly-once, но создаёт нагрузку на кластер: для каждого входного сообщения выполняется лукап и запись в таблицу дедупликации, плюс по одной записи на каждое выходное сообщение.

Swift снимает это ограничение: если функция детерминирована, хранить её вывод не нужно — при необходимости его можно воспроизвести. Это позволяет:

- снизить нагрузку на {{product-name}} до нуля или до минимума (только метаданные),
- повысить пропускную способность при stateless-преобразованиях.

## Как сохраняются гарантии exactly-once {#exactly-once}

Несмотря на отсутствие записи выходных данных в {{product-name}}, гарантии [exactly-once]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#exactly-once) сохраняются за счёт детерминированности:

- Если джоб упал до доставки результата, Flow перезапускает его и получает **тот же вывод** из тех же входных данных.
- [Message Distributor]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#message-distributor) продолжает доставлять сообщение до получения подтверждения (`MarkPersisted`) от получателя, что исключает потери.

Таким образом, exactly-once обеспечивается не хранением вывода, а **идемпотентностью** вычисления.

## Требование детерминированности {#determinism}

Функция преобразования в Swift-компьютейшене должна быть **детерминированной**: при одних и тех же входных данных должен возвращаться одинаковый вывод, включая порядок сообщений.

{% note warning %}

Нарушение детерминированности при обновлении пайплайна без [дрейна]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#start-stop-pause-pipeline) может привести к дубликатам или потере промежуточных сообщений: разные части системы могут обработать разные версии вывода.

{% endnote %}

Из этого правила могут быть исключения, обусловленные особенностями бизнес-логики пайплайна, но разработчик бизнес-логики должен точно представлять, почему он их реализует, и за счёт каких механизмов результат работы пайплайна в целом останется корректным.


## Классы Swift-компьютейшенов {#classes}

В Flow реализовано два базовых Swift-класса:

### TSwiftMapComputation {#swift-map}

Детерминированный Map без материализации результатов в {{product-name}}.

- **Нагрузка на {{product-name}}:** ~0 записей за эпоху (только системные фоновые процессы).
- **Не поддерживает:** [Source]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#source), [Sink]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#sink).
- **Поддерживает:** [таймеры]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#timer) и [key-visitor-стримы]({{ flow-docs-root }}/{{ lang }}/concepts/key_visitor{{ docs-revision-query }}) — только для работы со [стейтом]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#state), например для фонового клинапа (GC). Эмит выходных сообщений из обработки таймера или визита **запрещён**: output-стрим не может зависеть от таймер- или visit-стрима в `streams_dependency`. Так как таймер-стримы по умолчанию добавляются в зависимости каждого output-а, спека с таймерами и output-ами обязана задавать `streams_dependency` явно.
- **Требует:** строгой детерминированности и того, чтобы у каждого результирующего сообщения был ровно один родитель — входное сообщение.

Подробнее — в разделе [Computation (C++)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/cpp/computation{{ docs-revision-query }}#tswiftmapcomputation).

### TSwiftPassthroughComputation {#swift-passthrough-map}

[Passthrough-компьютейшен]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#passthrough) — наследник `TSwiftMapComputation`. Конвертирует `input`-сообщения в схему `output`-стрима без пользовательской логики. Подробнее — [Computation (C++)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/cpp/computation{{ docs-revision-query }}#tswiftpassthroughcomputation).

### TSwiftOrderedSourceComputation {#swift-source}

Основной класс для чтения упорядоченных данных из внешних источников.

- **Нагрузка на {{product-name}}:** ~1–2 записи за эпоху (метаданные для восстановления; сами сообщения не сохраняются).
- **Поддерживает:** `WatermarkStrategy` для оценки [вотермарков]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#timestamps-and-watermarks).
- **Требует:** ровно одного [Source]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#source), реализующего `IOrderedSource`.

Подробнее — в разделе [Computation (C++)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/cpp/computation{{ docs-revision-query }}#tswiftorderedsourcecomputation).

### TSwiftPassthroughOrderedSourceComputation {#swift-passthrough-source}

Passthrough-компьютейшен — наследник `TSwiftOrderedSourceComputation`. Преобразует `source`-сообщения в `output`-стрим приведением к новой схеме. Подробнее — [Computation (C++)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/cpp/computation{{ docs-revision-query }}#tswiftpassthroughorderedsourcecomputation).

## Сравнение с TTransformComputation {#comparison}

| Тип | Запись в {{product-name}} за эпоху | Поддержка таймеров | Поддержка стейта | Требование детерминированности |
|-----|-----------------------------------|--------------------|------------------|-------------------------------|
| `TTransformComputation` | 2 на каждое входное сообщение и 1 на выходное | Да | Да | Нет |
| `TSwiftOrderedSourceComputation` | ~1–2 (метаданные) | Нет | Нет | Да |
| `TSwiftPassthroughOrderedSourceComputation` | ~1–2 (метаданные) | Нет | Нет | Да |
| `TSwiftMapComputation` | ~0 | Да (только стейт) | Да | Да |
| `TSwiftPassthroughComputation` | ~0 | Нет | Нет | Да |

## См. также

- [Computation]({{ flow-docs-root }}/{{ lang }}/concepts/computation{{ docs-revision-query }})
- [Гарантии обработки]({{ flow-docs-root }}/{{ lang }}/concepts/guarantees{{ docs-revision-query }})
- [Computation (C++)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/cpp/computation{{ docs-revision-query }})
- [Computation (Java)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/java/computation{{ docs-revision-query }})
- [Computation (Python)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/computation{{ docs-revision-query }})
- [Computation (Go)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/go/computation{{ docs-revision-query }})
