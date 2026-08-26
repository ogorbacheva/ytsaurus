# StateAccessor в {{product-name}} Flow (Java)

StateAccessor — интерфейс для чтения, модификации и удаления значений [стейта]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#state).
Общие сведения о stateful-обработке описаны в разделе [Stateful processing]({{ flow-docs-root }}/{{ lang }}/concepts/stateful{{ docs-revision-query }}).

## Принцип работы {#how-it-works}

[Стейт]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#state) во Flow хранится в [сортированных динамических таблицах]({{ core-docs-root }}/{{ lang }}/concepts/dynamic-tables/sorted-dynamic-tables{{ docs-revision-query }}).
В случае [внешнего стейта]({{ flow-docs-root }}/{{ lang }}/how-to-guides/java/external-state{{ docs-revision-query }}) эта таблица создаётся пользователем, в случае [внутреннего стейта]({{ flow-docs-root }}/{{ lang }}/how-to-guides/java/internal-state{{ docs-revision-query }}) эти таблицы создаются и управляются Flow автоматически.

Далее для простоты описания будем рассматривать пример внешнего стейта.

Каждую строку в таблице стейта можно условно разделить на ключевые колонки и колонки значений:

![](../../../../_images/state_line_example.svg)

Для `TTransformCompanionComputation` ключевые колонки в таблице стейта совпадают с `group_by_schema` [компьютейшена]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#stream-and-computation). Для внутреннего стейта `TTransformOrderedSourceCompanionComputation` ключом служит ключ партиции источника: `group_by_schema` в таком SourceComputation не поддерживается.

Колонки значений будут доступны для чтения и модификации через `StateAccessor`. Формат, в котором эти значения будут доступны для чтения и модификации в Java-коде, зависит от реализации `StateAccessor`.

## Чтение и запись данных {#reading-and-writing-data}

Непосредственную работу с таблицей (чтение, запись, удаление данных) осуществляет [воркер]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#worker). При получении очередного батча [сообщений]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#message) воркер загружает значения стейтов для всех [ключей]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#key) в батче и отправляет их в [компаньон]({{ flow-docs-root }}/{{ lang }}/concepts/companion{{ docs-revision-query }}) вместе с сообщениями и [таймерами]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#timer). Подробнее про [схему взаимодействия]({{ flow-docs-root }}/{{ lang }}/concepts/companion{{ docs-revision-query }}#schema).

Запись новых значений в таблицу стейта осуществляется транзакционно в рамках [эпохи]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#epoch).

## Интерфейс StateAccessor {#state-accessor-interface}

{% list tabs group=lang %}

- Java

  ```java
  public interface StateAccessor<T> {
      /** Получить значение стейта. */
      Optional<T> get();

      /** Получить значение стейта или дефолтное значение. */
      default T getOrDefault(T defaultValue);

      /** Установить значение стейта. */
      void set(T value);

      /** Очистить/удалить стейт для ключа. */
      void clear();

      /** Получить класс стейта. */
      Class<T> getStateClass();
  }
  ```

- Kotlin

  ```kotlin
  interface StateAccessor<T> {
      /** Получить значение стейта. */
      fun get(): Optional<T>

      /** Получить значение стейта или дефолтное значение. */
      fun getOrDefault(defaultValue: T): T

      /** Установить значение стейта. */
      fun set(value: T)

      /** Очистить/удалить стейт для ключа. */
      fun clear()

      /** Получить класс стейта. */
      fun getStateClass(): Class<T>
  }
  ```

{% endlist %}
