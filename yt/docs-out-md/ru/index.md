---
metadata:
  - name: generator
    content: Diplodoc Platform v5.5.3
  - property: og:title
    content: Документация YTsaurus
csp:
  - script-src-elem:
      - https://mc.yandex.ru
  - connect-src:
      - https://*.algolia.net
      - https://*.algolianet.com
title: Документация YTsaurus
vcsPath: ru/index.md
sourcePath: ru/index.md
---

# YTsaurus

<style scoped>
.grid-container {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
  column-gap: 50px;
  row-gap: 20px;
}
.grid-item {
  display: flex;
  flex-direction: column;
}
.last {
  grid-column: -2;
}
h2 {
  padding-top: 32px !important;
  margin-top: 0 !important;
}
h3 {
  padding-top: 8px !important;
  margin-top: 0 !important;
}
</style>

**YTsaurus** (читается _вай-ти-за́у-рус_) — платформа распределенного хранения и обработки больших объемов данных с поддержкой [MapReduce](http://ru.wikipedia.org/wiki/MapReduce), распределенной файловой системой и NoSQL key-value базой данных.

<div class="grid-container">
    <div class="grid-item">
        <h3><a lang="ru" href="overview/about">Обзор</a></h3>
        <p>Общее описание системы: назначение YTsaurus и основные возможности платформы.</p>
    </div>
    <div class="grid-item">
        <h3><a lang="ru" href="user-guide/storage/cypress">Хранение информации</a></h3>
        <p>Хранение данных в YTsaurus: дерево метаинформации Кипарис, основные объекты системы, ACL, статические таблицы, транзакции, форматы хранения.</p>
    </div>
    <div class="grid-item">
        <h3><a lang="ru" href="overview/try-yt">Как попробовать</a></h3>
        <p>Примеры базовых действий c YTsaurus в CLI и веб-интерфейсе.</p>
    </div>
    <div class="grid-item">
        <h3><a lang="ru" href="user-guide/dynamic-tables/overview">Динамические таблицы</a></h3>
        <p>NoSQL key-value база данных: транзакции, язык запросов, реплицированные динамические таблицы.</p>
    </div>
    <div class="grid-item">
        <h3><a lang="ru" href="api/commands">API и справочник</a></h3>
        <p>Команды и их параметры, описание SDK и примеры кода для взаимодействия с платформой.</p>
    </div>
    <div class="grid-item">
        <h3><a lang="ru" href="user-guide/data-processing/scheduler/scheduler-and-pools">Обработка данных</a></h3>
        <p>Обработка данных при помощи YTsaurus: планировщик, парадигма MapReduce, поддерживаемые операции.</p>
        <ul>
            <li><b><a lang="ru" href="yql/index">YQL</a></b> — декларативный SQL-подобный язык запросов.</li>
            <li><b><a lang="ru" href="user-guide/data-processing/chyt/about-chyt">CHYT</a></b> — кластер ClickHouse внутри YTsaurus.</li>
            <li><b><a lang="ru" href="user-guide/data-processing/spyt/overview">SPYT</a></b> — кластер Apache Spark внутри YTsaurus.</li>
        </ul>
    </div>
</div>

## Полезные ссылки { #links }


* [GitHub](https://github.com/ytsaurus/ytsaurus)
* [Сайт YTsaurus](https://ytsaurus.tech/ru)
* [Telegram](https://t.me/ytsaurus_ru)
* [Stack Overflow](https://stackoverflow.com/tags/ytsaurus)
* [Рассылка для вопросов](mailto:community_ru@ytsaurus.tech)

