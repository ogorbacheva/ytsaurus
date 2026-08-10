# Overview

## What is Spark? { #what-is-spark }

[Apache Spark](https://spark.apache.org/) is an engine for computations using big data (joins, groups, filters, ets.).

Spark uses RAM to process its data. The key difference between computing in memory and the "conventional" 2005 MapReduce model is that the data has minimum disk impact, which minimizes I/O costs as the slowest part of computing. For a single Map transaction, the effect from using Spark will be negligible. However, even a single Map and Reduce sequence saves on writing intermediate results out to disk provided there is enough memory.

For each subsequent MapReduce sequence the efficiencies mount, and you can cache the output. For large and complex analytics pipelines, efficiency will increase many fold.

Spark is also equipped by a full-scale [Catalyst](https://github.com/tupol/spark-catalyst-study/blob/master/docs/catalyst-description.md) query optimizer that plans execution and takes into consideration:
- Input data location and size.
- Predicate pushdown to the file system.
- Expediency and step sequence in query execution.
- Collection of final table attributes.
- Use of local data for processing.
- Potential for computation pipelining.




## What is SPYT? { #what-is-spyt }

SPYT powered by Apache Spark enables a Spark cluster to be started with {{product-name}} computational capacity.  The cluster is started in a [{{product-name}} Vanilla operation]({{ core-docs-root }}/{{ lang }}/user-guide/reference/data-processing/operations/vanilla{{ docs-revision-query }}), then takes a certain amount of resources from the quota and occupies them constantly.  Spark can read [static]({{ core-docs-root }}/{{ lang }}/user-guide/explanation/storage/static-tables{{ docs-revision-query }}), as well as [dynamic {{product-name}} tables]({{ core-docs-root }}/{{ lang }}/user-guide/explanation/dynamic-tables/overview{{ docs-revision-query }}), perform computations on them, and record the result in the static table.


## Compatibility of SPYT versions with Apache Spark{ #spyt-compatibility }, Java, Scala, and Python

#|
|| **SPYT version** | **Spark version** | **Java** | **Scala** | **Python** ||
|| 1.x.x, 2.0.x | 3.2.2 | 11 | 2.12 | 3.8, 3.9, 3.11, 3.12 ||
|| 2.1.x, 2.2.x | 3.2.2 - 3.2.4 | 11 | 2.12 | 3.8, 3.9, 3.11, 3.12 ||
|| 2.3.x, 2.4.x | 3.2.2 - 3.3.4 | 11 | 2.12 | 3.8, 3.9, 3.11, 3.12 ||
|| 2.5.0 | 3.2.2 - 3.5.3 | 11 | 2.12 | 3.8, 3.9, 3.11, 3.12 ||
|| 2.6.x, 2.7.x, 2.8.x | 3.2.2 - 3.5.7 | 11, 17 | 2.12 | 3.8, 3.9, 3.11, 3.12 ||
|| 2.9.x | 3.2.2 - 3.5.8 | 11, 17 | 2.12 | 3.8, 3.9, 3.11, 3.12, 3.13 ||
|| 2.10.x | 3.3.0 - 4.1.x | 17 | 2.12, 2.13 | 3.11, 3.12, 3.13 ||
|#


## When to use SPYT { #what-to-do }

SPYT is optimal in the following cases:
- Developing in Java and using MapReduce in {{product-name}}.
- Optimizing pipeline performance on {{product-name}} with two or more joins or groupings.
- Writing integrational ETL pipelines from other storage systems.
- Ad-hoc analytics in interactive mode using `Jupyter`, `pyspark`, `spark-shell`, or the [Query Tracker]({{ core-docs-root }}/{{ lang }}/user-guide/explanation/query-tracker/about{{ docs-revision-query }}) component built into the UI.

Do not use SPYT if:
- You need to process over 10 TB of data in a single transaction.
- Your processing boils down to individual Map or MapReduce operations.

## Ways to run Spark calculations in {{product-name}} { #submit }

- Submitting directly to {{product-name}} using the `spark-submit` command. [Learn more]({{ docs_root }}/spyt/user-guide/how-to-guides/run-spark-jobs.md#submit).
- Creating a Standalone Spark cluster as a persistent resource within {{product-name}} using a Vanilla operation. [Learn more]({{ docs_root }}/spyt/user-guide/how-to-guides/run-spark-jobs.md#standalone).

## Supported programming languages { #lang }

Spark supports the following languages and development environments:

* [Jupyter]({{ docs_root }}/spyt/user-guide/how-to-guides/develop/jupyter.md).
* [Python]({{ docs_root }}/spyt/user-guide/how-to-guides/develop/python.md).
* [Java]({{ docs_root }}/spyt/user-guide/how-to-guides/develop/java.md).
* [Scala]({{ docs_root }}/spyt/user-guide/how-to-guides/develop/scala.md).

