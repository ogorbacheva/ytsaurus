# CHYT

**CHYT powered by ClickHouse** — это технология, которая позволяет поднять кластер из серверов ClickHouse непосредственно на вычислительных узлах {{product-name}}.

[ClickHouse](https://clickhouse.com/) поднимается внутри Vanilla-операции и работает с данными, которые лежат в {{product-name}}. Такая операция называется *кликой* (подробнее можно прочитать в разделе [Концепции]({{ docs_root }}/chyt/user-guide/explanation/general.md#what-is)). Клики бывают публичные (доступные всем пользователям) и приватные (личная клика пользователя или команды). Публичная клика `ch_public` — основная общедоступная клика, запущенная на каждом кластере {{product-name}}.

{{ chyt-user-guide-explanation-about-chyt-md-audience-1 }}

## Основные преимущества { #advantages }

Подавляющее большинство родной функциональности ClickHouse доступно в CHYT. Ознакомиться с богатыми возможностями ClickHouse можно в [официальной документации](https://clickhouse.com/docs/ru/).

Помимо этого есть следующие плюсы:
- Не нужно копировать данные из {{product-name}} в ClickHouse.
- Можно использовать вычислительную квоту в {{product-name}} для быстрых вычислений.
- Можно быстро проводить вычисления над данными в {{product-name}} небольшого и среднего объемов (до 1 ТБ), до 100 раз быстрее чем запуск MapReduce операции.
- Поддерживается работа со [статическими таблицами]({{ core-docs-root }}/{{ lang }}/user-guide/explanation/storage/static-tables{{ docs-revision-query }}) и [динамическими таблицами]({{ core-docs-root }}/{{ lang }}/user-guide/explanation/dynamic-tables/overview{{ docs-revision-query }}).

## Ограничения { #disadvantages }

Обрабатываемые таблицы должны быть [схематизированы]({{ core-docs-root }}/{{ lang }}/user-guide/reference/storage/static-schema{{ docs-revision-query }}).




