# StateAccessor in {{product-name}} Flow (Java)

StateAccessor is an interface for reading, modifying, and deleting state values. For general information about stateful processing, see the [Stateful processing]({{ flow-docs-root }}/{{ lang }}/concepts/stateful{{ docs-revision-query }}) section.

## How it works {#how-it-works}

The [state]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#state) in Flow is stored in [sorted dynamic tables]({{ core-docs-root }}/{{ lang }}/concepts/dynamic-tables/sorted-dynamic-tables{{ docs-revision-query }}).
If you use [external state]({{ flow-docs-root }}/{{ lang }}/how-to-guides/java/external-state{{ docs-revision-query }}), you create this table. If you use [internal state]({{ flow-docs-root }}/{{ lang }}/how-to-guides/java/internal-state{{ docs-revision-query }}), Flow creates and manages these tables automatically.

For simplicity, the following description focuses on an example with external state.

You can think of each row in the state table as having two parts: key columns and value columns:

![](../../../../_images/state_line_example.svg)

The key columns in the state table must match the `group_by_schema` of the [computation]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#stream-and-computation) that uses this state.

The value columns are available for reading and modifying through `StateAccessor`. The format in which you can read and modify these values in Java code depends on the `StateAccessor` implementation.

## Reading and writing data {#reading-and-writing-data}

The [worker]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#worker) handles direct operations on the table, including reading, writing, and deleting data. When the worker receives the next batch of [messages]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#message), it loads the state values for all [keys]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#key) in the batch and sends them to the [companion]({{ flow-docs-root }}/{{ lang }}/concepts/companion{{ docs-revision-query }}) along with the messages and [timers]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#timer). For more details, see the [interaction schema]({{ flow-docs-root }}/{{ lang }}/concepts/companion{{ docs-revision-query }}#schema).

You write new values to the state table as a transaction within an [epoch]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#epoch).

## StateAccessor interface {#state-accessor-interface}

{% list tabs group=lang %}

- Java

  ```java
  public interface StateAccessor<T> {
      /** Get the state value. */
      Optional<T> get();

      /** Get the state value or a default value. */
      default T getOrDefault(T defaultValue);

      /** Set the state value. */
      void set(T value);

      /** Clear or delete the state for the key. */
      void clear();

      /** Get the state class. */
      Class<T> getStateClass();
  }
  ```

- Kotlin

  ```kotlin
  interface StateAccessor<T> {
      /** Get the state value. */
      fun get(): Optional<T>

      /** Get the state value or a default value. */
      fun getOrDefault(defaultValue: T): T

      /** Set the state value. */
      fun set(value: T)

      /** Clear or delete the state for the key. */
      fun clear()

      /** Get the state class. */
      fun getStateClass(): Class<T>
  }
  ```

{% endlist %}