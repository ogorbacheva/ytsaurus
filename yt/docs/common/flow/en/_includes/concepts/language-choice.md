## Choose a language {#choose-language}

Flow supports five languages for implementing business logic:

- **[C++]({{ flow-docs-root }}/{{ lang }}/tutorials/cpp/quick-start{{ docs-revision-query }})** — native implementation, maximum performance, full control. Use this for high-load pipelines.
- **[Java and Kotlin]({{ flow-docs-root }}/{{ lang }}/tutorials/java/quick-start{{ docs-revision-query }})** — run via the [companion]({{ flow-docs-root }}/{{ lang }}/concepts/companion{{ docs-revision-query }}) mechanism. They support Spring Boot. These are suitable for teams with a JVM stack.
- **[Python]({{ flow-docs-root }}/{{ lang }}/tutorials/python/quick-start{{ docs-revision-query }})** — runs via the [companion]({{ flow-docs-root }}/{{ lang }}/concepts/companion{{ docs-revision-query }}) mechanism. This is the easiest way to prototype a pipeline or process a small data stream.
- **[Go]({{ flow-docs-root }}/{{ lang }}/tutorials/go/quick-start{{ docs-revision-query }})** — runs via the [companion]({{ flow-docs-root }}/{{ lang }}/concepts/companion{{ docs-revision-query }}) mechanism. A single binary runs the pipeline and acts as a companion in the job. Suitable for teams with a Go stack.
- **[YQL]({{ flow-docs-root }}/{{ lang }}/tutorials/yql/quick-start{{ docs-revision-query }})** — declarative pipeline description as an SQL query. It has a low entry barrier and doesn’t require writing code in C++, Java, Kotlin, Go, or Python. It’s under active development, and not all planned features are available yet.