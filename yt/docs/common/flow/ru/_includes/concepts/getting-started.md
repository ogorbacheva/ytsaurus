# С чего начать в {{product-name}} Flow

В этом разделе пошагово описано, как во Flow реализовать и запустить свой собственный пайплайн.

{% include [Выбор языка](language-choice.md) %}

## Общий план

Независимо от выбранного языка, создание пайплайна включает следующие шаги:

1. **Попробуйте [Быстрый старт]({{ flow-docs-root }}/{{ lang }}/tutorials/quick-start{{ docs-revision-query }})** — запустите минимальный NoOp-пайплайн, чтобы познакомиться с инфраструктурой Flow.

2. **Ознакомьтесь с основными понятиями**. Прочитайте [глоссарий]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}), чтобы понять модель Flow: пайплайны, потоки, компьютейшены, сообщения.

3. **Изучите концепции**. Разберитесь с [Computation]({{ flow-docs-root }}/{{ lang }}/concepts/computation{{ docs-revision-query }}), [Watermarks и Timers]({{ flow-docs-root }}/{{ lang }}/concepts/watermarks{{ docs-revision-query }}) и [Stateful-обработкой]({{ flow-docs-root }}/{{ lang }}/concepts/stateful{{ docs-revision-query }}), а также предоставляемыми системой [гарантиями]({{ flow-docs-root }}/{{ lang }}/concepts/guarantees{{ docs-revision-query }}).

4. **Изучите примеры** на выбранном языке:
   - C++: [WordCount]({{ flow-docs-root }}/{{ lang }}/tutorials/cpp/examples/word_count{{ docs-revision-query }}), [Shuffle]({{ flow-docs-root }}/{{ lang }}/tutorials/cpp/examples/shuffle{{ docs-revision-query }}), [WaitClickJoin]({{ flow-docs-root }}/{{ lang }}/tutorials/cpp/examples/wait_click_join{{ docs-revision-query }})
   - Java: [WordCount]({{ flow-docs-root }}/{{ lang }}/tutorials/java/examples/wordcount{{ docs-revision-query }}), [Shuffle]({{ flow-docs-root }}/{{ lang }}/tutorials/java/examples/shuffle{{ docs-revision-query }}), [WaitClickJoin]({{ flow-docs-root }}/{{ lang }}/tutorials/java/examples/wait_click_join{{ docs-revision-query }})
   - Python: [WordCount]({{ flow-docs-root }}/{{ lang }}/tutorials/python/examples/wordcount{{ docs-revision-query }}), [Shuffle]({{ flow-docs-root }}/{{ lang }}/tutorials/python/examples/shuffle{{ docs-revision-query }}), [WaitClickJoin]({{ flow-docs-root }}/{{ lang }}/tutorials/python/examples/wait_click_join{{ docs-revision-query }})
   - Go: [WordCount]({{ flow-docs-root }}/{{ lang }}/tutorials/go/examples/wordcount{{ docs-revision-query }}), [Shuffle]({{ flow-docs-root }}/{{ lang }}/tutorials/go/examples/shuffle{{ docs-revision-query }}), [WaitClickJoin]({{ flow-docs-root }}/{{ lang }}/tutorials/go/examples/wait_click_join{{ docs-revision-query }})
   - YQL: [Быстрый старт]({{ flow-docs-root }}/{{ lang }}/tutorials/yql/quick-start{{ docs-revision-query }})

5. **Ознакомьтесь с доступными [коннекторами]({{ flow-docs-root }}/{{ lang }}/concepts/connectors/overview{{ docs-revision-query }})** — очереди, статические таблицы{% if audience == "internal" %}, Logbroker{% endif %} и др.

6. **Опишите спеку пайплайна** в формате YSON. Помимо примеров, вам поможет раздел [Spec & DynamicSpec]({{ flow-docs-root }}/{{ lang }}/concepts/spec{{ docs-revision-query }}).

7. **Реализуйте бизнес-логику** на выбранном языке, следуя соответствующему руководству по быстрому старту.

8. **Создайте необходимые объекты в {{product-name}}** — таблицы, очереди, пайплайн{% if audience == "internal" %} — с помощью утилиты [YtSync]({{yt-sync-docs}}/) (спецификация пайплайна описана [здесь]({{yt-sync-docs}}/pipeline_specification)){% endif %}.{% if audience == "internal" %} При необходимости сделайте то же самое в сторонних системах вроде [Logbroker](../../flow/extensions/logbroker.md).{% endif %}

9. **Напишите тесты.** Следуйте инструкциям для выбранного языка программирования:
   - [C++]({{ flow-docs-root }}/{{ lang }}/how-to-guides/cpp/testing{{ docs-revision-query }})
   - [Java]({{ flow-docs-root }}/{{ lang }}/how-to-guides/java/testing{{ docs-revision-query }})
   - [Python]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/testing{{ docs-revision-query }})
   - [Go]({{ flow-docs-root }}/{{ lang }}/how-to-guides/go/testing{{ docs-revision-query }})

10. **Запустите пайплайн** и следите за ним через UI {{product-name}}. Детально про релизы можно прочитать в [Релизы и управление пайплайном]({{ flow-docs-root }}/{{ lang }}/how-to-guides/deployment/update-pipeline{{ docs-revision-query }}#release-and-configure-basic-rules).

## См. также

- [О Flow]({{ flow-docs-root }}/{{ lang }}/concepts/overview{{ docs-revision-query }})
- [Быстрый старт]({{ flow-docs-root }}/{{ lang }}/tutorials/quick-start{{ docs-revision-query }})
- [Основные понятия]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }})
- [Коннекторы]({{ flow-docs-root }}/{{ lang }}/concepts/connectors/overview{{ docs-revision-query }})
{% if audience == "internal" %}- [Сравнение с альтернативными технологиями](../../flow/other/comparison.md){% endif %}