<div id="ytsaurus-docs-landing"></div>

# YTsaurus documentation

<p class="yt-landing-lead">Understand the system. Get things done.</p>

<div class="yt-landing-search" role="search" aria-label="Search documentation">
  <label class="yt-landing-search-label" for="yt-landing-search">Search documentation</label>
  <div class="yt-landing-search-field">
    <span class="yt-landing-search-icon" aria-hidden="true"></span>
    <input id="yt-landing-search" type="search" placeholder="Search documentation" disabled>
  </div>
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
      text: How YTsaurus works
      textSize: m
    url: '{{ core-docs-root }}/{{ lang }}/concepts/overview{{ docs-revision-query }}'
    urlTitle: How YTsaurus works
    border: none
    animated: false
    text: Learn how the platform stores data, distributes computation, and connects cluster components.
    additionalInfo: Read the platform overview →
  - type: basic-card
    className: yt-landing-system-link
    title: Architecture
    url: '{{ core-docs-root }}/{{ lang }}/concepts/architecture{{ docs-revision-query }}'
    urlTitle: Architecture
    border: none
    animated: false
  - type: basic-card
    className: yt-landing-system-link
    title: Core concepts
    url: '{{ core-docs-root }}/{{ lang }}/concepts/basic-concepts{{ docs-revision-query }}'
    urlTitle: Core concepts
    border: none
    animated: false
  - type: basic-card
    className: yt-landing-system-link
    title: Cluster components
    url: '{{ core-docs-root }}/{{ lang }}/reference/components{{ docs-revision-query }}'
    urlTitle: Cluster components
    border: none
    animated: false
:::

<div class="yt-landing-start">

**Get started:** [Quick start]({{ core-docs-root }}/{{ lang }}/tutorials/quick-start{{ docs-revision-query }}) · [Python API]({{ core-docs-root }}/{{ lang }}/how-to-guides/api/python/start{{ docs-revision-query }}) · [Command line]({{ core-docs-root }}/{{ lang }}/how-to-guides/api/cli/install{{ docs-revision-query }})

</div>

## Put it into practice {#tasks}

Choose an area of work. Each section brings together core concepts and practical guides.

<nav class="yt-landing-task-index" aria-label="Task areas">

[Data storage](index.md#storage)
[Data processing](index.md#processing)
[Administration](index.md#administration)
[Machine learning](index.md#ml)

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
    text: Data storage
    textSize: sm
  description: |-
    Create objects, store large tables, and work with data that changes over time.

    **Start with the basics**

    [Cypress]({{ core-docs-root }}/{{ lang }}/concepts/storage/cypress{{ docs-revision-query }})

    [Static tables]({{ core-docs-root }}/{{ lang }}/concepts/storage/static-tables{{ docs-revision-query }})

    [Dynamic tables]({{ core-docs-root }}/{{ lang }}/concepts/dynamic-tables/overview{{ docs-revision-query }})
  children:
  - type: basic-card
    className: yt-landing-task
    title: Work with Cypress
    url: '{{ core-docs-root }}/{{ lang }}/how-to-guides/storage/cypress-example{{ docs-revision-query }}'
    urlTitle: Work with Cypress
    border: none
    animated: false
    text: Create objects and manage the metadata tree
  - type: basic-card
    className: yt-landing-task
    title: Update data from the command line
    url: '{{ core-docs-root }}/{{ lang }}/tutorials/dynamic-tables/add-remove-line{{ docs-revision-query }}'
    urlTitle: Update data from the command line
    border: none
    animated: false
    text: Create a dynamic table, then add and delete rows
  - type: basic-card
    className: yt-landing-task
    title: Create a backup
    url: '{{ core-docs-root }}/{{ lang }}/tutorials/dynamic-tables/backup-table{{ docs-revision-query }}'
    urlTitle: Create a backup
    border: none
    animated: false
    text: Back up and restore a dynamic table
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
    text: Data processing
    textSize: sm
  description: |-
    Run batch jobs, SQL queries, and stream processing over shared data.

    **Start with the basics**

    [Distributed computing]({{ core-docs-root }}/{{ lang }}/concepts/data-processing/overview{{ docs-revision-query }})

    [Scheduler and pools]({{ core-docs-root }}/{{ lang }}/concepts/data-processing/scheduler/scheduler-and-pools{{ docs-revision-query }})

    [Query Tracker]({{ core-docs-root }}/{{ lang }}/concepts/query-tracker/about{{ docs-revision-query }})
  children:
  - type: basic-card
    className: yt-landing-task
    title: Run a YQL query
    url: '{{ yql-docs-root }}/{{ lang }}/yql/{{ docs-revision-query }}'
    urlTitle: Run a YQL query
    border: none
    animated: false
    text: Process data with a SQL-like query language
  - type: basic-card
    className: yt-landing-task
    title: Run a Spark job
    url: '{{ spyt-docs-root }}/{{ lang }}/how-to-guides/run-spark-jobs{{ docs-revision-query }}'
    urlTitle: Run a Spark job
    border: none
    animated: false
    text: Use Apache Spark to process data in YTsaurus
  - type: basic-card
    className: yt-landing-task
    title: Run a CHYT query
    url: '{{ chyt-docs-root }}/{{ lang }}/user-guide/how-to-guides/try-chyt{{ docs-revision-query }}'
    urlTitle: Run a CHYT query
    border: none
    animated: false
    text: Run ClickHouse queries against data in YTsaurus
  - type: basic-card
    className: yt-landing-task
    title: Run a Flow pipeline
    url: '{{ flow-docs-root }}/{{ lang }}/tutorials/quick-start{{ docs-revision-query }}'
    urlTitle: Run a Flow pipeline
    border: none
    animated: false
    text: Build stateful event processing with delivery guarantees
  - type: basic-card
    className: yt-landing-task
    title: Connect Apache Flink
    url: '{{ flyt-docs-root }}/{{ lang }}/{{ docs-revision-query }}'
    urlTitle: Connect Apache Flink
    border: none
    animated: false
    text: Work with dynamic tables through FLYT Connector
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
    text: Administration
    textSize: sm
  description: Deploy a cluster, manage access, and configure its components.
  children:
  - type: basic-card
    className: yt-landing-task
    title: Deploy a cluster
    url: '{{ core-docs-root }}/{{ lang }}/how-to-guides/install-ytsaurus{{ docs-revision-query }}'
    urlTitle: Deploy a cluster
    border: none
    animated: false
    text: Prepare a specification and install YTsaurus
  - type: basic-card
    className: yt-landing-task
    title: Configure access
    url: '{{ core-docs-root }}/{{ lang }}/how-to-guides/cluster-access-proxy/overview{{ docs-revision-query }}'
    urlTitle: Configure access
    border: none
    animated: false
    text: Set up external access to a cluster in Kubernetes
  - type: basic-card
    className: yt-landing-task
    title: Change the configuration
    url: '{{ core-docs-root }}/{{ lang }}/reference/config-overrides{{ docs-revision-query }}'
    urlTitle: Change the configuration
    border: none
    animated: false
    text: Override settings for server components
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
    text: Machine learning
    textSize: sm
  description: Develop in notebooks and run demanding Spark workloads.
  children:
  - type: basic-card
    className: yt-landing-task
    title: Work in Jupyter
    url: '{{ spyt-docs-root }}/{{ lang }}/how-to-guides/development/jupyter{{ docs-revision-query }}'
    urlTitle: Work in Jupyter
    border: none
    animated: false
    text: Develop and run Spark applications in notebooks
  - type: basic-card
    className: yt-landing-task
    title: Use GPUs
    url: '{{ spyt-docs-root }}/{{ lang }}/how-to-guides/use-gpu{{ docs-revision-query }}'
    urlTitle: Use GPUs
    border: none
    animated: false
    text: Run SPYT computations on GPUs
  - type: basic-card
    className: yt-landing-task
    title: Optimize Spark jobs
    url: '{{ spyt-docs-root }}/{{ lang }}/how-to-guides/optimize-jobs{{ docs-revision-query }}'
    urlTitle: Optimize Spark jobs
    border: none
    animated: false
    text: Tune data reads, resource usage, and application execution
:::

## Documentation by service {#services}

Go straight to the documentation for the tool you need.

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
