# Pipeline operations in {{product-name}} Flow

After the [initial deployment]({{ flow-docs-root }}/{{ lang }}/how-to-guides/deployment/launch-vanilla{{ docs-revision-query }}), you manage the pipeline{% if audience == "internal" %} through the {{product-name}} UI or{% endif %} through the [CLI]({{ flow-docs-root }}/{{ lang }}/reference/cli{{ docs-revision-query }}). The main operations are start, stop, and pause:

* `start-pipeline` — start the pipeline;
* `stop-pipeline` — stop the pipeline through `draining` mode (a full flush of the intermediate buffers);
* `pause-pipeline` — stop the pipeline immediately.

For more about pipeline states, see the [glossary]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#start-stop-pause-pipeline).

These commands control the pipeline state, not the Vanilla operation itself: stopping the operation and recreating it when a new release is deployed are described in [Updates and releases]({{ flow-docs-root }}/{{ lang }}/how-to-guides/deployment/update-pipeline{{ docs-revision-query }}).

## See also

- [{{product-name}} Flow CLI]({{ flow-docs-root }}/{{ lang }}/reference/cli{{ docs-revision-query }})
- [Initial deployment]({{ flow-docs-root }}/{{ lang }}/how-to-guides/deployment/launch-vanilla{{ docs-revision-query }})
- [Updates and releases]({{ flow-docs-root }}/{{ lang }}/how-to-guides/deployment/update-pipeline{{ docs-revision-query }})
- [Security and access]({{ flow-docs-root }}/{{ lang }}/how-to-guides/deployment/configure-security{{ docs-revision-query }})
- [Spec and DynamicSpec]({{ flow-docs-root }}/{{ lang }}/concepts/spec{{ docs-revision-query }})
