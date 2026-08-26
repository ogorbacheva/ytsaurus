# Flow CLI

В рамках стандартной консольной утилиты `yt` существует специальный режим `flow` с набором команд для работы с Flow [Pipeline]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#pipeline). В этом разделе перечислены основные команды. Все актуальные опции каждой команды можно посмотреть с помощью команды `--help`.

#|
|| **Команда** | **Описание** ||
|| `start-pipeline` | Запустить Pipeline ||
|| `stop-pipeline` | Остановить Pipeline через режим `draining` ||
|| `pause-pipeline` | Остановить Pipeline немедленно ||
|| `get-pipeline-state` | Получить текущее состояние Pipeline ||
|| `get-flow-view` | Посмотреть Flow View &mdash; описание всего пайплайна с точки зрения контроллера ||
|| `get-pipeline-spec`, `set-pipeline-spec` | Посмотреть/изменить текущую [Spec]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#spec-and-dynamic-spec). Требует остановки Pipeline ||
|| `get-pipeline-dynamic-spec`, `set-pipeline-dynamic-spec` | Посмотреть/изменить текущую [DynamicSpec]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#spec-and-dynamic-spec) ||
|| `read-states` | Прочитать стейты пайплайна. Возвращает секции `key_states`, `partition_states`, `external_key_states`, `joined_external_key_states`. Фильтрация по `computation_id` / `partition_id` / `key` / `name` / `target`; `limit` применяется к каждой секции независимо. См. `TReadStatesArg` и `TReadStatesResponse` в [справочнике]({{ flow-docs-root }}/{{ lang }}/reference/configuration/all_yson_structs{{ docs-revision-query }}) ||
|| `delete-states` | Удалить стейты пайплайна. По умолчанию dry-run: возвращает счётчики совпавших строк, не удаляя их. Требует Stopped/Completed либо `force=true` на Paused. Удаляются только key/partition/manager-стейты; joiner-стейты не трогаются. См. `TDeleteStatesArg` и `TDeleteStatesResponse` в [справочнике]({{ flow-docs-root }}/{{ lang }}/reference/configuration/all_yson_structs{{ docs-revision-query }}) ||
|#

## См. также

- [Базовые правила выкатки]({{ flow-docs-root }}/{{ lang }}/how-to-guides/deployment/update-pipeline{{ docs-revision-query }}#release-and-configure-basic-rules)
- [Spec и DynamicSpec]({{ flow-docs-root }}/{{ lang }}/concepts/spec{{ docs-revision-query }})
