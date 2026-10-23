<div id="ytsaurus-docs-landing"></div>

# Документация YTsaurus

<p class="yt-landing-lead">Понять систему. Решить задачу.</p>

<div class="yt-landing-search" role="search" aria-label="Поиск по документации">
  <label class="yt-landing-search-label" for="yt-landing-search">Поиск по документации</label>
  <div class="yt-landing-search-field">
    <span class="yt-landing-search-icon" aria-hidden="true"></span>
    <input id="yt-landing-search" type="search" placeholder="Поиск по документации" disabled aria-describedby="yt-landing-search-status">
  </div>
  <p id="yt-landing-search-status">Поиск пока недоступен</p>
</div>

<div id="system" class="yt-landing-anchor"></div>

::: page-constructor
blocks:
- type: card-layout-block
  className: yt-landing-system
  animated: false
  colSizes:
    all: 12
  children:
  - type: basic-card
    className: yt-landing-overview
    title:
      text: Как устроен YTsaurus
      textSize: m
    url: '{{ core-docs-root }}/{{ lang }}/concepts/overview{{ docs-revision-query }}'
    urlTitle: Как устроен YTsaurus
    border: none
    animated: false
    text: Разберитесь, как платформа хранит данные, распределяет вычисления и объединяет компоненты кластера.
    additionalInfo: Прочитать обзор платформы →
  - type: basic-card
    className: yt-landing-system-link
    title: Архитектура
    url: '{{ core-docs-root }}/{{ lang }}/concepts/architecture{{ docs-revision-query }}'
    urlTitle: Архитектура
    border: none
    animated: false
  - type: basic-card
    className: yt-landing-system-link
    title: Основные понятия
    url: '{{ core-docs-root }}/{{ lang }}/concepts/basic-concepts{{ docs-revision-query }}'
    urlTitle: Основные понятия
    border: none
    animated: false
  - type: basic-card
    className: yt-landing-system-link
    title: Компоненты кластера
    url: '{{ core-docs-root }}/{{ lang }}/reference/components{{ docs-revision-query }}'
    urlTitle: Компоненты кластера
    border: none
    animated: false
:::

<div class="yt-landing-start">

**Начать работу:** [Быстрый старт]({{ core-docs-root }}/{{ lang }}/tutorials/quick-start{{ docs-revision-query }}) · [Python API]({{ core-docs-root }}/{{ lang }}/how-to-guides/api/python/start{{ docs-revision-query }}) · [Командная строка]({{ core-docs-root }}/{{ lang }}/how-to-guides/api/cli/install{{ docs-revision-query }})

</div>

## Перейти к практике {#tasks}

Выберите область работы. В каждом разделе собраны базовые понятия и готовые инструкции.

<nav class="yt-landing-task-index" aria-label="Области задач">

[Хранение данных](index.md#storage)
[Обработка данных](index.md#processing)
[Администрирование](index.md#administration)
[Машинное обучение](index.md#ml)

</nav>

<div id="storage" class="yt-landing-anchor"></div>

::: page-constructor
blocks:
- type: card-layout-block
  className: yt-landing-domain yt-landing-domain-storage
  animated: false
  colSizes:
    all: 12
  title:
    text: Хранение данных
    textSize: sm
  description: |-
    Создавайте объекты, храните большие таблицы и работайте с изменяемыми данными.

    **Сначала разобраться**

    [Кипарис]({{ core-docs-root }}/{{ lang }}/concepts/storage/cypress{{ docs-revision-query }})

    [Статические таблицы]({{ core-docs-root }}/{{ lang }}/concepts/storage/static-tables{{ docs-revision-query }})

    [Динамические таблицы]({{ core-docs-root }}/{{ lang }}/concepts/dynamic-tables/overview{{ docs-revision-query }})
  children:
  - type: basic-card
    className: yt-landing-task
    title: Работать с Кипарисом
    url: '{{ core-docs-root }}/{{ lang }}/how-to-guides/storage/cypress-example{{ docs-revision-query }}'
    urlTitle: Работать с Кипарисом
    border: none
    animated: false
    text: Создать объекты и управлять деревом метаинформации
  - type: basic-card
    className: yt-landing-task
    title: Изменять данные из консоли
    url: '{{ core-docs-root }}/{{ lang }}/tutorials/dynamic-tables/add-remove-line{{ docs-revision-query }}'
    urlTitle: Изменять данные из консоли
    border: none
    animated: false
    text: Создать динамическую таблицу, добавить и удалить строки
  - type: basic-card
    className: yt-landing-task
    title: Создать резервную копию
    url: '{{ core-docs-root }}/{{ lang }}/tutorials/dynamic-tables/backup-table{{ docs-revision-query }}'
    urlTitle: Создать резервную копию
    border: none
    animated: false
    text: Сохранить и восстановить состояние динамической таблицы
:::

<div id="processing" class="yt-landing-anchor"></div>

::: page-constructor
blocks:
- type: card-layout-block
  className: yt-landing-domain yt-landing-domain-processing
  animated: false
  colSizes:
    all: 12
  title:
    text: Обработка данных
    textSize: sm
  description: |-
    Запускайте пакетные, SQL- и потоковые вычисления поверх общих данных.

    **Сначала разобраться**

    [Распределённые вычисления]({{ core-docs-root }}/{{ lang }}/concepts/data-processing/overview{{ docs-revision-query }})

    [Планировщик и пулы]({{ core-docs-root }}/{{ lang }}/concepts/data-processing/scheduler/scheduler-and-pools{{ docs-revision-query }})

    [Query Tracker]({{ core-docs-root }}/{{ lang }}/concepts/query-tracker/about{{ docs-revision-query }})
  children:
  - type: basic-card
    className: yt-landing-task
    title: Выполнить YQL-запрос
    url: '{{ yql-docs-root }}/{{ lang }}/yql/{{ docs-revision-query }}'
    urlTitle: Выполнить YQL-запрос
    border: none
    animated: false
    text: Обработать данные с помощью SQL-подобного языка
  - type: basic-card
    className: yt-landing-task
    title: Запустить Spark-задачу
    url: '{{ spyt-docs-root }}/{{ lang }}/how-to-guides/run-spark-jobs{{ docs-revision-query }}'
    urlTitle: Запустить Spark-задачу
    border: none
    animated: false
    text: Использовать Apache Spark поверх данных YTsaurus
  - type: basic-card
    className: yt-landing-task
    title: Выполнить запрос в CHYT
    url: '{{ chyt-docs-root }}/{{ lang }}/user-guide/how-to-guides/try-chyt{{ docs-revision-query }}'
    urlTitle: Выполнить запрос в CHYT
    border: none
    animated: false
    text: Запустить ClickHouse-запрос к данным в YTsaurus
  - type: basic-card
    className: yt-landing-task
    title: Запустить Flow-пайплайн
    url: '{{ flow-docs-root }}/{{ lang }}/tutorials/quick-start{{ docs-revision-query }}'
    urlTitle: Запустить Flow-пайплайн
    border: none
    animated: false
    text: Обрабатывать поток событий с состоянием и гарантиями доставки
  - type: basic-card
    className: yt-landing-task
    title: Подключить Apache Flink
    url: '{{ flyt-docs-root }}/{{ lang }}/{{ docs-revision-query }}'
    urlTitle: Подключить Apache Flink
    border: none
    animated: false
    text: Работать с динамическими таблицами через FLYT Connector
:::

<div id="administration" class="yt-landing-anchor"></div>

::: page-constructor
blocks:
- type: card-layout-block
  className: yt-landing-domain yt-landing-domain-administration
  animated: false
  colSizes:
    all: 12
  title:
    text: Администрирование
    textSize: sm
  description: Разворачивайте кластер, управляйте доступами и меняйте конфигурацию.
  children:
  - type: basic-card
    className: yt-landing-task
    title: Развернуть кластер
    url: '{{ core-docs-root }}/{{ lang }}/how-to-guides/install-ytsaurus{{ docs-revision-query }}'
    urlTitle: Развернуть кластер
    border: none
    animated: false
    text: Подготовить спецификацию и установить YTsaurus
  - type: basic-card
    className: yt-landing-task
    title: Настроить доступы
    url: '{{ core-docs-root }}/{{ lang }}/how-to-guides/cluster-access-proxy/overview{{ docs-revision-query }}'
    urlTitle: Настроить доступы
    border: none
    animated: false
    text: Организовать внешний доступ к кластеру в Kubernetes
  - type: basic-card
    className: yt-landing-task
    title: Изменить конфигурацию
    url: '{{ core-docs-root }}/{{ lang }}/reference/config-overrides{{ docs-revision-query }}'
    urlTitle: Изменить конфигурацию
    border: none
    animated: false
    text: Переопределить параметры серверных компонентов
:::

<div id="ml" class="yt-landing-anchor"></div>

::: page-constructor
blocks:
- type: card-layout-block
  className: yt-landing-domain yt-landing-domain-ml
  animated: false
  colSizes:
    all: 12
  title:
    text: Машинное обучение
    textSize: sm
  description: Разрабатывайте в ноутбуках и запускайте ресурсоёмкие Spark-задачи.
  children:
  - type: basic-card
    className: yt-landing-task
    title: Работать в Jupyter
    url: '{{ spyt-docs-root }}/{{ lang }}/how-to-guides/development/jupyter{{ docs-revision-query }}'
    urlTitle: Работать в Jupyter
    border: none
    animated: false
    text: Разрабатывать и запускать Spark-приложения в ноутбуках
  - type: basic-card
    className: yt-landing-task
    title: Использовать GPU
    url: '{{ spyt-docs-root }}/{{ lang }}/how-to-guides/use-gpu{{ docs-revision-query }}'
    urlTitle: Использовать GPU
    border: none
    animated: false
    text: Запускать вычисления SPYT на графических ускорителях
  - type: basic-card
    className: yt-landing-task
    title: Оптимизировать Spark-задачи
    url: '{{ spyt-docs-root }}/{{ lang }}/how-to-guides/optimize-jobs{{ docs-revision-query }}'
    urlTitle: Оптимизировать Spark-задачи
    border: none
    animated: false
    text: Настроить чтение, ресурсы и выполнение приложений
:::

## Документация по сервисам {#services}

Если вы уже знаете нужный инструмент, перейдите прямо в его документацию.

::: page-constructor
blocks:
- type: card-layout-block
  className: yt-landing-services
  animated: false
  colSizes:
    all: 12
  children:
  - type: basic-card
    className: yt-landing-service
    title: YQL
    url: '{{ yql-docs-root }}/{{ lang }}/yql/{{ docs-revision-query }}'
    urlTitle: YQL
    border: none
    animated: false
  - type: basic-card
    className: yt-landing-service
    title: SPYT
    url: '{{ spyt-docs-root }}/{{ lang }}/{{ docs-revision-query }}'
    urlTitle: SPYT
    border: none
    animated: false
  - type: basic-card
    className: yt-landing-service
    title: CHYT
    url: '{{ chyt-docs-root }}/{{ lang }}/user-guide/explanation/about-chyt{{ docs-revision-query }}'
    urlTitle: CHYT
    border: none
    animated: false
  - type: basic-card
    className: yt-landing-service
    title: Flow
    url: '{{ flow-docs-root }}/{{ lang }}/{{ docs-revision-query }}'
    urlTitle: Flow
    border: none
    animated: false
  - type: basic-card
    className: yt-landing-service
    title: FLYT
    url: '{{ flyt-docs-root }}/{{ lang }}/{{ docs-revision-query }}'
    urlTitle: FLYT
    border: none
    animated: false
:::
