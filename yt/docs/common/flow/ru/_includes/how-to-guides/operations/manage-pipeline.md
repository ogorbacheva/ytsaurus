# Базовые операции с пайплайном {{product-name}} Flow

После [первичного деплоя]({{ flow-docs-root }}/{{ lang }}/how-to-guides/deployment/launch-vanilla{{ docs-revision-query }}) пайплайном управляют{% if audience == "internal" %} через UI {{product-name}} или{% endif %} через [CLI]({{ flow-docs-root }}/{{ lang }}/reference/cli{{ docs-revision-query }}). Основные операции — запуск, остановка и пауза:

* `start-pipeline` — запустить пайплайн;
* `stop-pipeline` — остановить пайплайн через режим `draining` (полный сброс промежуточных буферов);
* `pause-pipeline` — остановить пайплайн немедленно.

Подробнее про состояния пайплайна — в [глоссарии]({{ flow-docs-root }}/{{ lang }}/concepts/glossary{{ docs-revision-query }}#start-stop-pause-pipeline).

Эти команды управляют состоянием пайплайна, а не самой Vanilla-операцией: остановка операции и её пересоздание при выкатке нового релиза описаны в разделе [Обновления и релизы]({{ flow-docs-root }}/{{ lang }}/how-to-guides/deployment/update-pipeline{{ docs-revision-query }}).

## Полное удаление пайплайна {#remove}

Чтобы полностью удалить пайплайн, отмените его vanilla-операцию и удалите узел пайплайна вместе с содержимым — включая таблицы стейта, поэтому операция необратима:

```bash
yt abort-op <operation-id>
yt remove -r //path/to/pipeline
```

Если выполнить `remove` сразу после отмены операции, некоторое время команда будет завершаться ошибкой вида `Cannot take "exclusive" lock ... leader_controller_lock`. Это ожидаемо: контроллер-лидер удерживает лок на узле `leader_controller_lock` под мастер-транзакцией, при отмене операции освободить его не успевает, и лок исчезает только после того, как мастер прекратит переставшую пинговаться транзакцию.

Дождаться освобождения можно, повторяя `yt remove -r` до успеха, либо проверяя лок явно:

```bash
yt get //path/to/pipeline/leader_controller_lock/@lock_count
```

Когда команда вернёт `0` (или узел уже удалён), пайплайн можно удалять.

## См. также

- [CLI {{product-name}} Flow]({{ flow-docs-root }}/{{ lang }}/reference/cli{{ docs-revision-query }})
- [Первичный деплой]({{ flow-docs-root }}/{{ lang }}/how-to-guides/deployment/launch-vanilla{{ docs-revision-query }})
- [Обновления и релизы]({{ flow-docs-root }}/{{ lang }}/how-to-guides/deployment/update-pipeline{{ docs-revision-query }})
- [Безопасность и доступы]({{ flow-docs-root }}/{{ lang }}/how-to-guides/deployment/configure-security{{ docs-revision-query }})
- [Spec и DynamicSpec]({{ flow-docs-root }}/{{ lang }}/concepts/spec{{ docs-revision-query }})
