# Веб-интерфейс клики в {{product-name}}

Веб‑интерфейс — удобный способ управлять [кликами]({{ docs_root }}/chyt/user-guide/explanation/general.md). Он подходит для:

- быстрых разовых операций;
- визуального контроля состояния клики;
- настройки без написания скриптов;
- оперативного изучения параметров благодаря наглядной структуре.

## Как перейти в интерфейс клики { #where }

1. Слева в главном меню {{product-name}} выберите пункт **Cliques**.
2. В разделе **CHYT cliques** выберите нужную клику из списка или [создайте новую]({{ docs_root }}/chyt/user-guide/how-to-guides/cliques/create-start-stop.md#create).

    {% note info %}

    Для знакомства с веб‑интерфейсом используйте публичную клику `ch_public`. Это общедоступная клика, которая работает на каждом кластере {{product-name}}.

    {% endnote %}

## Основные разделы веб-интерфейса { #ui }

{{ chyt-user-guide-reference-cliques-ui-md-audience-1 }}

_1. [Заголовок раздела](#header) — здесь указано название клики._  
_2. [Кнопки действий](#action-menu) — используйте их для управления кликами._  
_3. [Блок c характеристиками клики](#params) — здесь можно посмотреть состояние клики._  
_4. [Панель вкладок](#tabs) — ссылки на вкладки настроек, ACL и логов._

### Заголовок раздела {#header}

В заголовке (1) отображается базовая информация:

- название кластера, на котором находится клика, и кнопка для его смены;
- название раздела **CHYT cliques**;
- кнопки:
  - ![add to favourites](../../../../_images/user-guide/reference/cliques/add-to-favourites-btn.png){width=24 height=24} _Add to favourites_ — добавить клику в избранное;
  - ![view favourites](../../../../_images/user-guide/reference/cliques/view-favourites-btn.png){width=24 height=24} _View favourites_ — посмотреть избранные клики;
- название клики;
- кнопка **Create clique** для [создания новой клики]({{ docs_root }}/chyt/user-guide/how-to-guides/cliques/create-start-stop.md#create).

### Кнопки действий {#action-menu}

Справа расположен блок с кнопками действий (2):

- ![sql](../../../../_images/user-guide/reference/cliques/sql-btn.png){width=24 height=24} _SQL_ — переход в веб-интерфейс для выполнения запросов [Query Tracker]({{ core-docs-root }}/{{ lang }}/user-guide/explanation/query-tracker/about{{ docs-revision-query }});
- ![start](../../../../_images/user-guide/reference/cliques/start-btn.png){width=24 height=24} _Start_ — запуск клики;
- ![stop](../../../../_images/user-guide/reference/cliques/stop-btn.png){width=24 height=24} _Stop_ — остановка клики;
- ![remove](../../../../_images/user-guide/reference/cliques/remove-btn.png){width=24 height=24} _Remove_ — удаление клики;
- ![edit speclet](../../../../_images/user-guide/reference/cliques/edit-btn.png){width=24 height=24} _Edit speclet_ — редактирование [спеклета]({{ docs_root }}/chyt/user-guide/reference/cliques/configs.md#speclet) — файла настроек (конфигурации) клики.

### Характеристики клики {#params}

Под хедером находится блок с характеристиками клики (3). Важные параметры:

- `Health` — отображает жизнеспособность активной клики. Параметр может принимать значения:
  - `Good` — клика здорова и готова принимать запросы;
  - `Pending` — это состояние перед `Good`, показывает процесс ожидания запуска;
  - `Failed` — клика недоступна из-за сбоя.
  
  {% note info %}

  
  Когда параметр `Health` находится в значении `Failed`, [Strawberry Controller]({{ docs_root }}/chyt/user-guide/explanation/controller.md) перезапускает Vanilla-операцию. Если проблема устранена, `Health` перейдёт в состояние `Pending` и затем `Good`, или снова в состояние `Failed`, при котором контроллер снова будет пытаться перезапустить операцию.
  

  {% endnote %}
  
- `State` — состояние клики: `Active` / `Inactive`. Показывает, запущена клика или остановлена;
- `Pool` — название вычислительного [пула]({{ core-docs-root }}/{{ lang }}/user-guide/explanation/data-processing/scheduler/scheduler-and-pools{{ docs-revision-query }}#scheduler), которое является ссылкой на его веб-интерфейс;
- `Instances`, `Cores`, `Memory` — количество экземпляров и вычислительных ресурсов (процессоров CPU и оперативной памяти RAM), выделенных под клику;
- `YT operation Id` — ссылка на интерфейс [YT-операции]({{ docs_root }}/chyt/user-guide/reference/cliques/yt-operation-ui.md), которая соответствует клике.

Остальные характеристики служат для справки и помогают детализировать главные метрики и определять причины сбоев.

### Панель вкладок {#tabs}

Важная информация о клике отображается в блоке (4) на вкладках:

- **Speclet** — отображает содержимое YSON‑документа с настройками клики ([спеклета]({{ docs_root }}/chyt/user-guide/reference/cliques/configs.md#speclet)).
- **ACL** — показывает, какие права доступа к клике выданы и для каких групп пользователей. Подробнее в разделе [Права доступа]({{ docs_root }}/chyt/user-guide/reference/cliques/access.md).
- **Query Logs** — ведёт к таблице с логами запросов. Подробнее — в разделе [Получение Query Logs]({{ docs_root }}/chyt/user-guide/how-to-guides/queries/get-query-logs.md).

## Полезные ссылки

[Настройки клики]({{ docs_root }}/chyt/user-guide/reference/cliques/configs.md)  
