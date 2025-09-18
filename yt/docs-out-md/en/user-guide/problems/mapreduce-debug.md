---
metadata:
  - name: generator
    content: Diplodoc Platform v5.5.3
csp:
  - script-src-elem:
      - https://mc.yandex.ru
  - connect-src:
      - https://*.algolia.net
      - https://*.algolianet.com
vcsPath: en/user-guide/problems/mapreduce-debug.md
sourcePath: en/user-guide/problems/mapreduce-debug.md
---
# Debugging MapReduce programs

{% include [Local emulation](../../_includes/user-guide/problems/mapreduce-debug/local-emulation-40bee28c0ef6.md) %}

{% include [Stderr running](../../_includes/user-guide/problems/mapreduce-debug/stderr-running-6da9192e2db1.md) %}

## Getting full stderr of all jobs of an operation

In YTsaurus, you can save full stderr of all jobs to a table. You can export stderr of those jobs that were not aborted.

To enable the described behavior:

{% list tabs %}

- In Python

  Use the `stderr_table` parameter. For example:

  ```python
  yt.wrapper.run_map_reduce( mapper, reducer, '//path/to/input', '//path/to/output', reduce_by=['some_key'], stderr_table='//path/to/stderr/table', )
  ```
- In С++

  Use the [StderrTablePath](https://github.com/ytsaurus/ytsaurus/blob/07c9c385ae116f56d8ecce9fa6765fa1a90e95cc/yt/cpp/mapreduce/interface/operation.h#L563) setting.

- In SDKs in other languages

  You can pass the `stderr_table_path` setting directly to the operation specification. For a description of this option, see [Operation options](../data-processing/operations/operations-options.md).

{% endlist %}

{% include [Stderr table](../../_includes/user-guide/problems/mapreduce-debug/stderr-table-8a2620873ddb.md) %}
