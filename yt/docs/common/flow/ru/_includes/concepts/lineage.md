# Lineage в {{product-name}} Flow

Lineage *(с англ. родословная)* — это информация о том, из каких входных [сообщений]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#message) и [таймеров]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#timer) был получен конкретный выходной результат [компьютейшена]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#computation). Эта информация используется фреймворком в момент обработки для вычисления метаданных и обеспечения гарантий порядка, но не сохраняется в самом выходном сообщении.

## Зачем нужен lineage {#why-lineage}

Lineage используется фреймворком в двух целях:

1. **Вычисление метаполей.** На основе родительских сообщений Flow автоматически заполняет метаполя выходных сообщений: `EventTimestamp`, `AlignmentTimestamp` и другие. Для Swift-компьютейшенов и [passthrough]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#passthrough) `AlignmentTimestamp` наследуется от родителей без изменений — это гарантирует корректную [приоритизацию]({{ flow-docs-root }}/{{ lang }}/concepts/ordering{{ docs-revision-query }}) сообщений в downstream-компьютейшенах.

2. **Гарантии порядка производных сообщений.** Если у двух сообщений совпадают [ключи группировки]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#key) по всей цепочке lineage от источника до текущего компьютейшена, то их относительный порядок обработки сохраняется. Подробнее — в разделе [Порядок обработки сообщений]({{ flow-docs-root }}/{{ lang }}/concepts/ordering{{ docs-revision-query }}#ordering-guarantees).

## Поведение по умолчанию {#default-behavior}

В большинстве случаев явно управлять lineage не нужно — фреймворк устанавливает родителей автоматически:

| Тип функции | Родитель выходного сообщения |
|---|---|
| `RowFunction` / `DoProcessMessage` | текущее входное сообщение |
| `BatchFunction` / `DoProcess` | все сообщения текущего батча |
| Обработчик таймера | текущий таймер |

## Когда задавать lineage явно {#explicit-lineage}

По умолчанию родителем выходного сообщения считается весь текущий батч. Явный lineage позволяет сузить это множество до конкретного подмножества входных объектов, что делает вычисление `EventTimestamp` и `AlignmentTimestamp` более точным.

## API {#api}

Lineage устанавливается через метод `SetParents` / `set_parent_ids` / `setParentIds` / `WithParentIDs` на объекте `OutputCollector`. Метод возвращает **новый** коллектор с привязанным контекстом lineage — все вызовы `AddMessage` / `add_message` / `addMessage` на нём будут нести этот lineage.

Подробнее об использовании в каждом языке:
- [C++]({{ flow-docs-root }}/{{ lang }}/how-to-guides/cpp/computation{{ docs-revision-query }}#output-collector)
- [Java]({{ flow-docs-root }}/{{ lang }}/how-to-guides/java/computation{{ docs-revision-query }}#output-collector)
- [Python]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/computation{{ docs-revision-query }}#output-collector)
- [Go]({{ flow-docs-root }}/{{ lang }}/how-to-guides/go/computation{{ docs-revision-query }}#output-collector)

## См. также

- [Порядок обработки сообщений]({{ flow-docs-root }}/{{ lang }}/concepts/ordering{{ docs-revision-query }})
- [Computation]({{ flow-docs-root }}/{{ lang }}/concepts/computation{{ docs-revision-query }})
- [Основные понятия (глоссарий)]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }})
- [Computation (Go)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/go/computation{{ docs-revision-query }})
