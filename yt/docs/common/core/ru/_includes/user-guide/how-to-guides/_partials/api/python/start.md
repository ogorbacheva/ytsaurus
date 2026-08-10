## Python API

{% note info "Примечание" %}

Перед началом работы установите Python-клиент из pip-репозитория следующей командой:

```bash
pip install ytsaurus-client
```

{% endnote %}

После установки пакета становится доступным:

- Python библиотека [yt](https://pydoc.ytsaurus.tech/yt.html);
- Бинарный файл [yt]({{ docs_root }}/core/user-guide/reference/api/cli/cli);
{{ core-user-guide-how-to-guides-api-python-start-md-audience-1 }}

Текущая вресия пакета требует **Python 3.8+**

### Установка { #install }

#### Библиотеки YSON

Для использования YSON формата для работы с таблицами потребуются C++ биндинги, которые устанавливаются отдельным пакетом. Установка [YSON биндингов]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#yson_bindings):

   ```bash
   pip install ytsaurus-yson
   ```

{% note warning "Внимание" %}

В настоящий момент нет возможности установить YSON биндинги под Windows.

{% endnote %}

{% note info "Для пользователей платформы Apple M1" %}

В настоящий момент нет YSON биндингов, собранных под платформу Apple. В качестве временного решения можно воспользоваться [Rosetta 2](https://ru.wikipedia.org/wiki/Rosetta_(программное_обеспечение)) и установить версию Python для архитектуры x86_64.

Подробнее об этом можно прочитать по [ссылке](https://stackoverflow.com/questions/71691598/how-to-run-python-as-x86-with-rosetta2-on-arm-macos-machine).


{% endnote %}

Подробнее про YSON можно прочитать в разделе [Форматы]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#formats).

Чтобы узнать версию установленной обертки из Python, распечатайте переменную `yt.VERSION` или вызовите команду `yt --version`.

При возникновении проблем ознакомьтесь с разделом [FAQ](#faq). Если проблема сохранилась, напишите в [чат](https://t.me/ytsaurus_ru).

[Исходный код библиотеки](https://github.com/ytsaurus/ytsaurus/tree/main/yt/python/yt/wrapper).

{% note warning "Внимание" %}

Не рекомендуется устанавливать библиотеку и зависимые от нее пакеты разными способами одновременно. Это может приводить к трудно диагностируемым проблемам.

{% endnote %}

#### Дополнительные зависимости (extras) { #extras }

Часть функциональности требует дополнительных зависимостей, которые не ставятся по умолчанию. Они объявлены в наборах extras пакета `ytsaurus-client` и устанавливаются указанием набора в квадратных скобках:

| Набор | Зависимости | Для чего нужен |
| --- | --- | --- |
| `recommended` | `brotli`, `cryptography` | Рекомендуемые опциональные зависимости |
| `admin` | `kubernetes`, `docker` | [Административные команды CLI]({{ docs_root }}/core/admin-guide/reference/cli-admin) (`yt admin logs k8s`, `yt admin metrics replay`) |

```bash
pip install "ytsaurus-client[recommended]"
pip install "ytsaurus-client[admin]"
```

### Документация для пользователей { #userdoc }
  * [Общее]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#common)
    - [Соглашения, использующиеся в коде]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#agreements)
    - [Клиент]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#client)
      - [Потокобезопасность]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#threadsafety)
      - [Асинхронный клиента на основе gevent]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#gevent)
    - [Конфигурация]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#configuration)
      - [Общий конфиг]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#configuration_common)
      - [Настройка логирования]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#configuration_logging)
      - [Настройка токена]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#configuration_token)
      - [Настройка ретраев]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#configuration_retries)
    - [Ошибки]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#errors)
    - [Форматы]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#formats)
    - [YPath]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#ypath)
  * [Команды]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#commands)
    - [Работа с кипарисом]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#cypress_commands)
    - [Работа с файлами]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#file_commands)
    - [Работа с таблицами]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#table_commands)
      - [Датаклассы]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#dataclass)
      - [Схемы]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#table_schema)
      - [TablePath]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#tablepath_class)
      - [Команды]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#table_commands)
      - [Параллельное чтение таблиц]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#parallel_read)
      - [Параллельная запись таблиц]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#parallel_write)
    - [Работа с транзакциями и локами]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#transaction_commands)
    - [Запуск операций]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#run_operation_commands)
      - [SpecBuilder]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#spec_builder)
    - [Работа с операциями и джобами]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#operation_and_job_commands)
      - [Operation]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#operation_class)
      - [OperationsTracker]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#operations_tracker_class)
      - [OperationsTrackerPool]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#operations_tracker_pool_class)
    - [Работы с правами доступа]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#acl_commands)
    - [Работа с динамическими таблицами]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#dyntables_commands)
    - [Другие команды]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#etc_commands)
  * [Python-объекты в качестве операций]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#python_operations)
    - [Общая информация]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#python_operations_intro)
    - [Подготовка операции из джоба]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#prepare_operation)
    - [Декораторы]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#python_decorators)
    - [Pickling функции и окружения]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#pickling)
      - [Запуск Python-нагрузок в open-source окружении]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#python_open_source_environment)
      - [Запуск локального скрипта внутри Docker с помощью `respawn_in_docker`]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#respawn_in_docker)
      - [Общее устройство]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#pickling_description)
      - [Ссылка на пост с советами]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#pickling_advises)
    - [Porto-слои]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#porto_layers)
    - [tmpfs в джобах]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#tmpfs_in_jobs)
    - [Статистики в джобах]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#python_jobs_statistics)
  * [Нетипизированные Python-операции]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#python_operations_untyped)
    - [Декораторы]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#python_decorators_untyped)
    - [Форматы]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#python_formats)
       - [Структурированное представление данных]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#structured_data)
       - [Контрольные атрибуты]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#control_attributes)
       - [Другие форматы]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#other_formats)
  * [Другое]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#other)
    - [gRPC]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#grpc)
    - [YSON-биндинги]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#yson_bindings)
  * [Устаревшее]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#legacy)
    - [Python3 и байтовые строки]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#python3_strings)

### Справка { #pydoc }
Самая актуальная справка по конкретным функциям и их параметрам находится в коде.

Посмотреть описание функций и классов в интерпретаторе можно следующим образом:

```bash
$ python
>>> import yt.wrapper as yt
>>> help(yt.run_sort)
```

### Примеры { #examples }

  * [Базовый уровень]({{ docs_root }}/core/user-guide/how-to-guides/api/python/examples#base)
    - [Чтение и запись таблиц]({{ docs_root }}/core/user-guide/how-to-guides/api/python/examples#read_write)
    - [Схемы таблиц]({{ docs_root }}/core/user-guide/how-to-guides/api/python/examples#table_schema)
    - [Простой map]({{ docs_root }}/core/user-guide/how-to-guides/api/python/examples#simple_map)
    - [Сортировка таблицы и простая операция reduce]({{ docs_root }}/core/user-guide/how-to-guides/api/python/examples#sort_and_reduce)
    - [Reduce с несколькими входными таблицами]({{ docs_root }}/core/user-guide/how-to-guides/api/python/examples#reduce_multiple_output)
    - [Reduce с несколькими входными и несколькими выходными таблицами]({{ docs_root }}/core/user-guide/how-to-guides/api/python/examples#reduce_multiple_input_output)
    - [MapReduce]({{ docs_root }}/core/user-guide/how-to-guides/api/python/examples#map_reduce)
    - [MapReduce с несколькими промежуточными таблицами]({{ docs_root }}/core/user-guide/how-to-guides/api/python/examples#map_reduce_multiple_intermediate_streams)
    - [Декораторы для классов-джобов]({{ docs_root }}/core/user-guide/how-to-guides/api/python/examples#job_decorators)
    - [Работа с файлами на клиенте и в операциях]({{ docs_root }}/core/user-guide/how-to-guides/api/python/examples#files)
    - [Генеричный grep]({{ docs_root }}/core/user-guide/how-to-guides/api/python/examples#grep)
  * [Продвинутый уровень]({{ docs_root }}/core/user-guide/how-to-guides/api/python/examples#advanced)
    - [Batch запросы]({{ docs_root }}/core/user-guide/how-to-guides/api/python/examples#batch_queries)
    - [RPC]({{ docs_root }}/core/user-guide/how-to-guides/api/python/examples#rpc)
  * [Разное]({{ docs_root }}/core/user-guide/how-to-guides/api/python/examples#misc)
    - [Датаклассы]({{ docs_root }}/core/user-guide/how-to-guides/api/python/examples#dataclass)
    - [Контекст и управление записью в выходные таблицы ]({{ docs_root }}/core/user-guide/how-to-guides/api/python/examples#table_switches)
    - [Spec builders]({{ docs_root }}/core/user-guide/how-to-guides/api/python/examples#spec_builder)
    - [Использование gevent]({{ docs_root }}/core/user-guide/how-to-guides/api/python/examples#gevent)

<!-- ### Для разработчика { #fordeveloper }

  * [Контрибы](for_developer.md#contribs)
  * [Разбиение библиотеки на части в Аркадии](for_developer.md#peerdirs)
  * [Устройство и запуск тестов](for_developer.md#tests)
  * [Политика обновления библиотеки](for_developer.md#update_policy) -->

### FAQ { #faq }

В данном разделе собраны ответы на ряд частых вопросов, касающихся Python API. Ответы на другие частые вопросы в разделе [FAQ]({{ docs_root }}/core/user-guide/reference/faq).

**Q: Установил пакет через pypi, но получаю ошибку `yt: command not found`.**
A: Попробуйте выполнить команду
`pip install ytsaurus-client --force-reinstall`
скорее всего в логе будет warning вида `The script yt is installed in '...' which isn't on your PATH`. Для решения проблемы необходимо добавить указанный путь в переменную окружения PATH. Для этого нужно выполнить следующую команду:

```
echo 'export PATH="$PATH:<указанный путь>"' >> ~/.bashrc
source ~/.bashrc
```
В зависимости от оболочки файл может называться по-другому. Чаще всего на Mac он называется `~/.zshrc`.

**Q: Чтение с retry завершается ошибкой из-за превышения таймаута.**
A: Скорее всего в таблице слишком много чанков, нужно укрупнить их. Используйте `yt merge --src table --dst table --spec "{combine_chunks=true}"`

**Q: Операция завершается с ошибкой YSON-а (например: `YsonError: Premature end of stream`), а в веб-интерфейсе появляется ошибка парсинга  YSON.**
A: Скорее всего, операция пишет в `stdout`. Это запрещено делать явно в Python через `print, sys.stdout.write()`, если операция не помечена как `raw_io`, но это может делать сторонняя программа, например, архиватор.

**Q: Python библиотека слишком много пишет в stderr, как повысить уровень логирования?**
A: Уровень можно повысить, установив переменную окружения `YT_LOG_LEVEL="ERROR"`, или через настройку логгера {{product-name}}: `logging.getLogger("Yt").setLevel(logging.ERROR)`.

**Q: Запускаю операцию с Mac OS X, а джобы завершаются с ошибками типа `ImportError: ./tmpfs/modules/_ctypes.so: invalid ELF header`.**
A: Так как Python wrapper забирает с собой на кластер все зависимости Python операции, то туда же приезжают бинарные .so и .pyc файлы, которые потом не могут быть загружены. Стоит использовать porto-слой с вашим локальным окружением, а также включить фильтрацию этих файлов, чтобы они не попадали на кластер. Подробнее можно прочитать в [разделе]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#porto_layers).

**Q: Джобы завершаются с ошибкой `Invalid table index N: expected integer in range [A,B]`.**
A: Сообщение означает, что в записях вы выдаете table index, причем соответствующей таблицы нет. Чаще всего это означает, что у вас несколько входных таблиц, а выходная таблица одна. Во входных записях по умолчанию приходят поля `@table_index`, чтобы их выключить, можно поменять формат: `yt.config["tabular_data_format"] = yt.YsonFormat(process_table_index=None)`. Подробнее про формат можно прочитать в [разделе]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#python_formats)). В качестве альтернативы явно укажите в спецификации (пример для map-операции):  `{"mapper": {"enable_input_table_index": False}}`.

**Q: При запуске операции, после того, как она стала completed, появляется ошибка (ReadTimeout, HTTPConnectionPool(....): Read timed out.).**
A: Сообщение означает, что не удалось скачать stderr операции из-за сетевых проблем, причём не помогли даже повторные запросы. В таком случае, стоит воспользоваться опцией `ignore_stderr_if_download_failed`, которая позволяет игнорировать stderr, если его не удалось скачать. Рекомендуется использовать опцию при написании production-процессов.

**Q: Получаю ошибку `Yson bindings required`.**
A: Это означает, что в качестве входного (выходного) формата выбран YSON и в джобе не удалось импортировать биндинги. Подробнее про YSON и биндинги к нему можно прочитать в [разделе]({{ docs_root }}/core/user-guide/how-to-guides/api/python/userdoc#yson). Нужно установить пакет с биндингами, а также проверить, что YSON биндинги не отфильтровываются с помощью `module_filter`. Это динамическая библиотека yson_lib.so, и ее можно легко нечаянно отфильтровать, если отфильтровывать все .so файлы. Кроме того, чтобы `yt_yson_bindings`, приехавшие в модулях, не удалялись, в файле конфигурации нужно прописать `config["pickling"]["ignore_yson_bindings_for_incompatible_platforms"] = False`.


