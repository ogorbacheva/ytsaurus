# Обзор

## Что такое Spark? { #what-is-spark }

[Apache Spark](https://spark.apache.org/) — это фреймворк для расчетов на больших данных (джойнов, группировок, фильтраций и т. д.).

Spark обрабатывает данные в оперативной памяти. Ключевое отличие процессинга в памяти от "классического" MapReduce образца 2005 года в том, что данные минимально затрагивают диск при работе, а значит, минимизируются расходы на IO — самую медленную часть процессинга. Для одиночной Map операции эффект от использования Spark не будет заметен. Но уже для одного каскада Map и Reduce удается избежать записи промежуточных результатов на диск при условии, что памяти будет достаточно.

Для каждого последующего каскада MapReduce экономия нарастает, появляется возможность кешировать результаты. Для больших и сложных аналитических пайплайнов рост производительности будет многократным.

Также Spark вооружен полноценным оптимизатором запросов [Catalyst](https://github.com/tupol/spark-catalyst-study/blob/master/docs/catalyst-description.md), который планирует выполнение и учитывает:
- расположение и объёмы входных данных;
- протягивание предикатов до файловой системы;
- целесообразность и порядок шагов при исполнении запроса;
- набор атрибутов в конечной таблице;
- локальность данных при обработке;
- возможную конвейеризацию вычислений.

## Как Spark интегрирован с {{product-name}}

Подробности интеграции Spark c {{product-name}} можно узнать в [вебинаре](https://youtu.be/gZ7m-2_LqB0).




## Что такое SPYT? { #what-is-spyt }

SPYT powered by Apache Spark позволяет запускать Spark-кластер на вычислительных мощностях {{product-name}}. Кластер запускается в [Vanilla-операции {{product-name}}]({{ core-docs-root }}/{{ lang }}/user-guide/reference/data-processing/operations/vanilla{{ docs-revision-query }}), затем забирает некоторое количество ресурсов из квоты и занимает их постоянно. Spark может читать как [статические]({{ core-docs-root }}/{{ lang }}/user-guide/explanation/storage/static-tables{{ docs-revision-query }}), так и [динамические таблицы {{product-name}}]({{ core-docs-root }}/{{ lang }}/user-guide/explanation/dynamic-tables/overview{{ docs-revision-query }}), делать на них расчеты и писать результат в статическую таблицу.


## Совместимость версий SPYT с версиями Apache Spark{ #spyt-compatibility }, Java, Scala, Python

#|
|| **Версия SPYT** | **Версия Spark** | **Java** | **Scala** | **Python** ||
|| 1.x.x, 2.0.x | 3.2.2 | 11 | 2.12 | 3.8, 3.9, 3.11, 3.12 ||
|| 2.1.x, 2.2.x | 3.2.2 - 3.2.4 | 11 | 2.12 | 3.8, 3.9, 3.11, 3.12 ||
|| 2.3.x, 2.4.x | 3.2.2 - 3.3.4 | 11 | 2.12 | 3.8, 3.9, 3.11, 3.12 ||
|| 2.5.0 | 3.2.2 - 3.5.3 | 11 | 2.12 | 3.8, 3.9, 3.11, 3.12 ||
|| 2.6.x, 2.7.x, 2.8.x | 3.2.2 - 3.5.7 | 11, 17 | 2.12 | 3.8, 3.9, 3.11, 3.12 ||
|| 2.9.x | 3.2.2 - 3.5.8 | 11, 17 | 2.12 | 3.8, 3.9, 3.11, 3.12, 3.13 ||
|| 2.10.x | 3.3.0 - 4.1.x | 17 | 2.12, 2.13 | 3.11, 3.12, 3.13 ||
|#


## Когда использовать SPYT { #what-to-do }

SPYT оптимален в следующих случаев:
- разработка на Java с использованием MapReduce в {{product-name}};
- оптимизация производительности пайплайна на {{product-name}} с двумя и более джойнами или группировками;
- написание интеграционных ETL пайплайнов из других систем хранения;
- ad-hoc аналитика в интерактивном режиме с использованием `Jupyter`, `pyspark`, `spark-shell` или встроенного в UI компонента [Query Tracker]({{ core-docs-root }}/{{ lang }}/user-guide/explanation/query-tracker/about{{ docs-revision-query }}).

SPYT не стоит выбирать, если:
- существует необходимость в обработке более 10 ТБ данных в одной транзакции;
- процессинг сводится к единичным Map или MapReduce.

## Способы запуска расчетов на Spark в {{product-name}} { #submit }

- Отдельные запуски расчётов напрямую в {{product-name}}, используя команду `spark-submit` [Подробнее]({{ docs_root }}/spyt/user-guide/how-to-guides/run-spark-jobs.md#submit).
- Создание Standalone Spark кластера как постоянного ресурса внутри {{product-name}} при помощи Vanilla операции [Подробнее]({{ docs_root }}/spyt/user-guide/how-to-guides/run-spark-jobs.md#standalone).

## На каких языках можно писать { #lang }

Spark поддерживает следующие языки и среды разработки:

* [Jupyter]({{ docs_root }}/spyt/user-guide/how-to-guides/develop/jupyter.md).
* [Python]({{ docs_root }}/spyt/user-guide/how-to-guides/develop/python.md).
* [Java]({{ docs_root }}/spyt/user-guide/how-to-guides/develop/java.md).
* [Scala]({{ docs_root }}/spyt/user-guide/how-to-guides/develop/scala.md).

