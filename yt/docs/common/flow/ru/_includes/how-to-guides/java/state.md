# Работа со стейтами в {{product-name}} Flow (Java)

{% note info %}

На этой странице описаны детали для Java и Kotlin при работе со стейтами. Общие концепции описаны в разделе [Stateful-вычисления]({{ flow-docs-root }}/{{ lang }}/concepts/stateful{{ docs-revision-query }}).

{% endnote %}

Java SDK Flow (Java и Kotlin) предоставляет несколько типов стейт-аксессоров для работы с состоянием. Наиболее часто используются:

- **YsonStateAccessor** — для YSON-стейтов, хранящихся во внутренних таблицах Flow.
- **ExternalStateAccessor** — для внешних стейтов, хранящихся в отдельных динамических таблицах {{product-name}}.

Также доступны **ProtoStateAccessor**, **DefaultStateAccessor**, **RawStateAccessor** и **NoOpStateAccessor**. Подробнее о всех типах — в разделе [Internal State]({{ flow-docs-root }}/{{ lang }}/how-to-guides/java/internal-state{{ docs-revision-query }}).

## YsonStateAccessor {#yson-state}

`YsonStateAccessor<T>` предоставляет типизированный доступ к YSON-стейту, привязанному к ключу сообщения. Получить аксессор можно через `RuntimeContext`:

{% list tabs group=lang %}

- Java

  ```java
  StateAccessor<T> stateAccessor = ctx.getYsonStateAccessor("state-name", message, StateClass.class);
  ```

- Kotlin

  ```kotlin
  val stateAccessor: StateAccessor<T> = ctx.getYsonStateAccessor("state-name", message, StateClass::class.java)
  ```

{% endlist %}

Параметры:
- `"state-name"` — имя стейта, должно совпадать с именем, зарегистрированным в [спеке]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#spec-and-dynamic-spec) [компьютейшена]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#stream-and-computation) (`parameters/internal_states`).
- `message` — текущее сообщение, из которого извлекается [ключ группировки]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#key).
- `StateClass.class` — Java-класс для сериализации/десериализации стейта.

### Методы StateAccessor

| Метод | Описание |
| --- | --- |
| `get()` | Получить текущее значение стейта (`Optional<T>`) |
| `set(T value)` | Установить новое значение стейта |
| `getOrDefault(T defaultValue)` | Получить значение или вернуть значение по умолчанию |
| `clear()` | Удалить стейт для текущего ключа |

### Пример: WordCountMapper

{% list tabs group=lang %}

- Java

  ```java
  @Component
  public class WordCountMapper implements RowFunction {

      @Override
      public void onMessage(ExtendedMessage message, OutputCollector output, RuntimeContext ctx) {
          Word input = message.getPayload();

          StateAccessor<WordCountState> stateAccessor =
                  ctx.getYsonStateAccessor("word-state", message, WordCountState.class);

          var state = stateAccessor.getOrDefault(new WordCountState(input.getWord(), 0));
          state.setCount(state.getCount() + 1);
          stateAccessor.set(state);
      }
  }
  ```

- Kotlin

  ```kotlin
  @Component
  class WordCountMapper : RowFunction {

      override fun onMessage(message: ExtendedMessage, output: OutputCollector, ctx: RuntimeContext) {
          val input: Word = message.getPayload()

          val stateAccessor = ctx.getYsonStateAccessor("word-state", message, WordCountState::class.java)

          val state = stateAccessor.getOrDefault(WordCountState(input.word, 0))
          state.count = state.count + 1
          stateAccessor.set(state)
      }
  }
  ```

{% endlist %}

В этом примере:
1. Из сообщения извлекается объект `Word` с полем `word`.
2. Получается аксессор для стейта `"word-state"`, привязанного к ключу текущего сообщения.
3. Если стейт для данного ключа отсутствует, создается новый объект `WordCountState` с начальным значением счетчика 0.
4. Значение счетчика увеличивается и стейт обновляется.

Класс стейта должен быть аннотирован `@YTreeObject` для сериализации в YSON:

{% list tabs group=lang %}

- Java

  ```java
  @YTreeObject
  public class WordCountState {
      private String word;
      private long count;

      public WordCountState() {}

      public WordCountState(String word, long count) {
          this.word = word;
          this.count = count;
      }

      // getters и setters
      public String getWord() { return word; }
      public void setWord(String word) { this.word = word; }
      public long getCount() { return count; }
      public void setCount(long count) { this.count = count; }
  }
  ```

- Kotlin

  ```kotlin
  @YTreeObject
  class WordCountState {
      var word: String = ""
      var count: Long = 0
      constructor()
      constructor(word: String, count: Long) {
          this.word = word
          this.count = count
      }
  }
  ```

{% endlist %}

## ExternalStateAccessor {#external-state}

`ExternalStateAccessor` предоставляет доступ к внешнему стейту, хранящемуся в отдельной динамической таблице {{product-name}}. Внешний стейт описывается константой `ExternalStateDescriptor`, создаваемой через `StateDescriptors.external(...)`:

{% list tabs group=lang %}

- Java

  ```java
  ExternalStateAccessor externalStateAccessor = ctx.getExternalStateAccessor("state-name", message);
  ```

- Kotlin

  ```kotlin
  val externalStateAccessor = ctx.getExternalStateAccessor("state-name", message)
  ```

{% endlist %}

### Методы ExternalStateAccessor

| Метод | Описание |
| --- | --- |
| `get()` | Получить текущее значение стейта (`Optional<Payload>`) |
| `getOrDefault()` | Получить значение или пустой `Payload` |
| `set(Payload value)` | Установить новое значение стейта |
| `clear()` | Удалить стейт для текущего ключа |

`Payload` — нетипизированный контейнер с доступом к полям по имени. Для модификации используется `PayloadBuilder`.

### Пример: EventReducer

{% list tabs group=lang %}

- Java

  ```java
  public class EventReducer implements RowFunction {

      @Override
      public void onMessage(ExtendedMessage message, OutputCollector output, RuntimeContext ctx) {
          ExternalStateAccessor externalStateAccessor =
                  ctx.getExternalStateAccessor("shuffle-state", message);

          Payload state = externalStateAccessor.getOrDefault();

          PayloadBuilder stateBuilder = state.toBuilder();
          if (state.get("count", Long.class) == null) {
              stateBuilder.set("count", 1L);
          } else {
              stateBuilder.set("count", state.get("count", Long.class) + 1);
          }

          externalStateAccessor.set(stateBuilder.finish());
      }
  }
  ```

- Kotlin

  ```kotlin
  class EventReducer : RowFunction {

      override fun onMessage(message: ExtendedMessage, output: OutputCollector, ctx: RuntimeContext) {
          val externalStateAccessor = ctx.getExternalStateAccessor("shuffle-state", message)

          val state = externalStateAccessor.getOrDefault()

          val stateBuilder = state.toBuilder()
          if (state.get("count", Long::class.java) == null) {
              stateBuilder.set("count", 1L)
          } else {
              stateBuilder.set("count", state.get("count", Long::class.java) + 1)
          }

          externalStateAccessor.set(stateBuilder.finish())
      }
  }
  ```

{% endlist %}

В этом примере:
1. Объявляется дескриптор `SHUFFLE_STATE` для внешнего стейта `"/shuffle-state"`.
2. Через `ctx.getState(SHUFFLE_STATE, message)` получается аксессор для ключа сообщения.
3. Текущее значение стейта извлекается как `Payload`.
4. С помощью `PayloadBuilder` создается обновленная версия стейта с увеличенным счетчиком.
5. Обновленный стейт сохраняется обратно.

## Стейт в таймерах {#state-in-timers}

При обработке таймеров стейт доступен через объект `timer`, который содержит [ключ группировки]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#key):

{% list tabs group=lang %}

- Java

  ```java
  @Override
  public void onTimer(Timer timer, OutputCollector output, RuntimeContext ctx) {
      ExternalStateAccessor stateAccessor =
              ctx.getExternalStateAccessor("join-state", timer);

      Payload joinState = stateAccessor.get().orElseThrow();

      // обработка стейта и генерация выходных сообщений
      var messageBuilder = ctx.createMessageBuilder("output_stream");
      messageBuilder.set("hit_id", joinState.get("hit_id", String.class));
      // ... заполнение остальных полей ...
      output.addMessage(messageBuilder.finish());

      // очистка стейта после обработки
      stateAccessor.clear();
  }
  ```

- Kotlin

  ```kotlin
  override fun onTimer(timer: Timer, output: OutputCollector, ctx: RuntimeContext) {
      val stateAccessor = ctx.getExternalStateAccessor("join-state", timer)

      val joinState = stateAccessor.get().orElseThrow()

      // обработка стейта и генерация выходных сообщений
      val messageBuilder = ctx.createMessageBuilder("output_stream")
      messageBuilder.set("hit_id", joinState.get("hit_id", String::class.java))
      // ... заполнение остальных полей ...
      output.addMessage(messageBuilder.finish())

      // очистка стейта после обработки
      stateAccessor.clear()
  }
  ```

{% endlist %}

Метод `clear()` удаляет стейт для данного ключа. Это важно делать после закрытия окна или финализации обработки, чтобы не накапливать устаревшие данные.

## Привязка стейта к ключу {#group-by}

В `TTransformCompanionComputation` ключ, по которому осуществляется доступ к стейту, определяется полем `group_by_schema` в спеке компьютейшена. Стейт-аксессор автоматически извлекает ключ из переданного сообщения или таймера.

В `TTransformOrderedSourceCompanionComputation` поле `group_by_schema` не поддерживается. Ключом внутреннего стейта служит ключ партиции источника, поэтому все сообщения одной партиции разделяют стейт. Подробнее о выборе класса для SourceComputation см. в разделе [Computation (Java)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/java/computation{{ docs-revision-query }}#sourcecomputation).

Подробнее про `group_by_schema` и его влияние на работу стейтов — в разделе [Stateful-вычисления]({{ flow-docs-root }}/{{ lang }}/concepts/stateful{{ docs-revision-query }}).

## Конфигурация стейта в спеке {#spec-config}

### YSON-стейт (internal_states)

YSON-стейты регистрируются в спеке компьютейшена через `parameters/internal_states`:

```yson
"computations" = {
    "mapper" = {
        "computation_class_name" = "NYT::NFlow::NCompanion::TTransformCompanionComputation";
        "parameters" = {
            "internal_states" = ["word-state"];
        };
    };
};
```

### Внешний стейт (external state)

Внешний стейт регистрируется в секции `external_state_managers` `Computation`. Имя должно начинаться с `/` и совпадать со значением, переданным в `StateDescriptors.external(...)`:

```yson
"computations" = {
    "reducer" = {
        "computation_class_name" = "NYT::NFlow::NCompanion::TTransformCompanionComputation";
        "external_state_managers" = {
            "/shuffle-state" = {
                "external_state_manager_class_name" = "NYT::NFlow::TSimpleExternalStateManager";
                "parameters" = {
                    "path" = "//path/to/state/table";
                };
            };
        };
        "parameters" = {};
    };
};
```

Поле `external_state_manager_class_name` задаёт зарегистрированный класс external state manager — для типового сценария это `"NYT::NFlow::TSimpleExternalStateManager"`. Подробнее про доступные менеджеры см. в [C++ документации]({{ flow-docs-root }}/{{ lang }}/how-to-guides/cpp/state{{ docs-revision-query }}#external-state).

Таблицу стейта необходимо создать заблаговременно{% if audience == "internal" %}, например с помощью [YtSync]({{yt-sync-docs}}/){% endif %}.

## См. также

- [Stateful-вычисления]({{ flow-docs-root }}/{{ lang }}/concepts/stateful{{ docs-revision-query }})
- [Internal State (Java)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/java/internal-state{{ docs-revision-query }})
- [External State (Java)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/java/external-state{{ docs-revision-query }})
- [Computation (Java)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/java/computation{{ docs-revision-query }})
- [Быстрый старт (Java)]({{ flow-docs-root }}/{{ lang }}/tutorials/java/quick-start{{ docs-revision-query }})
