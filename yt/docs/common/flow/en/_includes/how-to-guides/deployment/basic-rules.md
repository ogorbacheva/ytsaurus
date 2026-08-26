# Basic rules for deploying a Pipeline in {{product-name}} Flow

This section is the entry point to deploying and operating a Flow pipeline: it lists the available launch methods and the pages that cover each part of the deployment surface. The general rules for deploying releases and changing the {{product-name}} configuration, which do not depend on the launch method, are collected under YT sync rules below.

## How to launch {#launch-flow}

You can start the controllers and workers in the following ways:

* [Launch in a Vanilla operation]({{ flow-docs-root }}/{{ lang }}/how-to-guides/deployment/launch-vanilla{{ docs-revision-query }}) — the controllers and workers start within a single {{product-name}} vanilla operation; no separate long-running deployment is needed. This is the simplest method.{% if audience == "internal" %}
* **Launch via Infractl** — a long-running deployment of controllers and workers in YP. Recommended for production. This method is described in the internal Russian documentation only.{% endif %}

## Deployment and operations {#deployment-pages}

Once the pipeline runs, the rest of the deployment surface is covered by these pages:

* [Pipeline operations]({{ flow-docs-root }}/{{ lang }}/how-to-guides/operations/manage-pipeline{{ docs-revision-query }}) — start, stop, and pause a running pipeline.
* [Releases]({{ flow-docs-root }}/{{ lang }}/how-to-guides/deployment/update-pipeline{{ docs-revision-query }}) — roll out a new version, reanimate an aborted operation, and deploy a hotfix.
* [Security]({{ flow-docs-root }}/{{ lang }}/how-to-guides/deployment/configure-security{{ docs-revision-query }}) — the account the operation runs as, secret delivery, and the minimum required permissions.
* [Logs]({{ flow-docs-root }}/{{ lang }}/how-to-guides/operations/view-logs{{ docs-revision-query }}) — where the process logs are written and how to read them from a job.
* [YT sync rules]({{ flow-docs-root }}/{{ lang }}/how-to-guides/deployment/yt-sync-rules{{ docs-revision-query }}) — the general rules for deploying releases and for changing the table configuration in {{product-name}}.
* [Authentication]({{ flow-docs-root }}/{{ lang }}/how-to-guides/deployment/configure-authentication{{ docs-revision-query }}) — how the controllers and workers authenticate in {{product-name}}.

## See also

* [Launch in a Vanilla operation]({{ flow-docs-root }}/{{ lang }}/how-to-guides/deployment/launch-vanilla{{ docs-revision-query }})
* [Spec and DynamicSpec]({{ flow-docs-root }}/{{ lang }}/concepts/spec{{ docs-revision-query }})
* [Pipeline CLI]({{ flow-docs-root }}/{{ lang }}/reference/cli{{ docs-revision-query }})
* [Protection against zombie processes]({{ flow-docs-root }}/{{ lang }}/concepts/deployment/flow-core-target{{ docs-revision-query }})
