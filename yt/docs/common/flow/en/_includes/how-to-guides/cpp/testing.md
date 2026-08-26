# Testing in {{product-name}} Flow (C++)

{% note info %}

C++ [workers]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#worker) currently don't have a separate framework for unit testing computations. The main approach is to run the entire pipeline with final data sources. You can find a test example in [examples/cpp/wait_click_join]({{source-root}}/yt/yt/flow/examples/cpp/wait_click_join). If you still need to write unit tests, you can move the business logic out of the computations into separate classes and write unit tests for those classes.

{% endnote %}

{% include notitle [_](../../_partials/testing-integration-body.md) %}

{% include notitle [_](../../_partials/testing-test-param-body.md) %}

## See also

- [Basic release rules]({{ flow-docs-root }}/{{ lang }}/how-to-guides/deployment/basic-rules{{ docs-revision-query }})
- [Testing (Java)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/java/testing{{ docs-revision-query }})
- [Testing (Python)]({{ flow-docs-root }}/{{ lang }}/how-to-guides/python/testing{{ docs-revision-query }})
- [Quick start (C++)]({{ flow-docs-root }}/{{ lang }}/tutorials/cpp/quick-start{{ docs-revision-query }})
- [Quick start (Java)]({{ flow-docs-root }}/{{ lang }}/tutorials/java/quick-start{{ docs-revision-query }})
- [Quick start (Python)]({{ flow-docs-root }}/{{ lang }}/tutorials/python/quick-start{{ docs-revision-query }})