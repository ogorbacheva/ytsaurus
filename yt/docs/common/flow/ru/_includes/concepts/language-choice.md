## Выбор языка {#choose-language}

Flow поддерживает несколько языков для реализации бизнес-логики:

- **[C++]({{ flow-docs-root }}/{{ lang }}/tutorials/cpp/quick-start{{ docs-revision-query }})** — нативная реализация, максимальная производительность, полный контроль. Рекомендуется для высоконагруженных пайплайнов.
- **[Java и Kotlin]({{ flow-docs-root }}/{{ lang }}/tutorials/java/quick-start{{ docs-revision-query }})** — работают через механизм [компаньонов]({{ flow-docs-root }}/{{ lang }}/concepts/companion{{ docs-revision-query }}). Поддерживают Spring Boot. Подходят для команд с JVM-стеком.
- **[Python]({{ flow-docs-root }}/{{ lang }}/tutorials/python/quick-start{{ docs-revision-query }})** — работает через механизм [компаньонов]({{ flow-docs-root }}/{{ lang }}/concepts/companion{{ docs-revision-query }}). Самый простой способ создать прототип пайплайна или обрабатывать небольшой поток данных.
- **[Go]({{ flow-docs-root }}/{{ lang }}/tutorials/go/quick-start{{ docs-revision-query }})** — работает через механизм [компаньонов]({{ flow-docs-root }}/{{ lang }}/concepts/companion{{ docs-revision-query }}). Один бинарь одновременно запускает пайплайн и работает компаньоном в джобе. Подходит для команд с Go-стеком.
- **[YQL]({{ flow-docs-root }}/{{ lang }}/tutorials/yql/quick-start{{ docs-revision-query }})** — декларативное описание пайплайна в виде SQL-запроса. Минимальный порог входа, не требует написания кода на C++, Java, Kotlin, Go или Python. Находится в активной разработке, ещё не вся запланированная функциональность доступна.
