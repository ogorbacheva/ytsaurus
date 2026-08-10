#### **Q: Почему в CHYT есть клики, тогда как в обычном ClickHouse ничего похожего нет? Что такое клика?** {#chyt-cliques-explanation}

**A:** Про это есть отдельная [статья]({{ docs_root }}/chyt/user-guide/explanation/general.md).

------

#### **Q: Получаю одну из ошибок «DB::NetException: Connection refused», «DB::Exception: Attempt to read after eof: while receiving packet from». Что это значит?** {#connection-refused-error}

**A:** Типично такое означает, что процесс CHYT внутри операции Vanilla аварийно завершился. Можно посмотреть в UI операции на [счётчики]({{ docs_root }}/chyt/user-guide/reference/cliques/ui.md) числа aborted/failed джобов. Если есть недавние aborted-джобы по причине preemption, это значит, что клике не хватает ресурсов. Если есть недавние failed джобы, обратитесь к администратору системы.

------

#### **Q: Получаю ошибку «Subquery exceeds data weight limit: XXX > YYY». Что это значит?** {#subquery-data-weight-limit}

**A:** смотрите опцию `max_data_weight_per_subquery` в документации по [конфигурации]({{ docs_root }}/chyt/user-guide/reference/cliques/configuration.md#yt) клики.

------

#### **Q: Как сохранять в таблицу?** {#save-to-table}

**A:** Есть функции **INSERT INTO** и **CREATE TABLE**, Подробнее можно прочитать в разделе [Отличие от ClickHouse.]({{ docs_root }}/chyt/user-guide/reference/yt-tables.md#save)

------

#### **Q: Как загрузить геословари в собственной клике?** {#load-geodictionaries-chyt}

**A:** При старте любой клики можно указать опцию `--cypress-geodata-path`, которая позволяет указать путь к геословарям в Кипарисе. Подробнее про эту опцию можно прочитать в статье [Как попробовать]({{ docs_root }}/chyt/user-guide/how-to-guides/try-chyt.md).

------

#### **Q: CHYT умеет обрабатывать даты в формате TzDatetime?** {#chyt-tzdatetime-handling}

**A:** CHYT умеет обрабатывать даты в формате TzDatetime ровно в той же мере, в какой обычный ClickHouse. Хранить данные придётся в виде строк или чисел и конвертировать при чтении-записи. Пример извлечения даты:

```sql
toDate(reinterpretAsInt64(reverse(unhex(substring(hex(payment_dt), 1, 8)))))
```

{{ chyt-user-guide-reference-faq-md-audience-1 }}

------

#### **Q: Как переложить таблицу на SSD?** {#how-to-set-ssd}

**A:** Для начала необходимо убедиться, что в вашем аккаунте в {{product-name}} квота в медиуме **ssd_blobs**. Для этого можно на {{ chyt-user-guide-reference-faq-md-audience-2 }} переключить тип медиума на **ssd_blobs** и ввести название своего аккаунта. Если квоты в медиуме **ssd_blobs** нет, то ее можно запросить через специальную форму.

После получения квоты на медиуме **ssd_blobs** необходимо изменить значение атрибута `primary_medium`, данные будут в фоне переложены на соответствующий медиум. Подробнее можно прочитать в разделе про [хранение]({{ core-docs-root }}/{{ lang }}/user-guide/reference/faq{{ docs-revision-query }}#storage).

Для статических таблиц можно форсировать перекладывание с помощью операции [Merge]({{ core-docs-root }}/{{ lang }}/user-guide/reference/data-processing/operations/merge{{ docs-revision-query }}):

```bash
yt set //home/dev/test_table/@primary_medium ssd_blobs
yt merge --mode auto --spec '{"force_transform"=true;}' --src //home/dev/test_table --dst //home/dev/test_table
```

Если таблица динамическая, для изменения медиума нужно предварительно отмонтировать таблицу,
установить атрибут, а затем смонтировать обратно:

```bash
yt unmount-table //home/dev/test_table --sync
yt set //home/dev/test_table/@primary_medium ssd_blobs
yt mount-table //home/dev/test_table --sync
```

Дополнительно ускорить перекладывание можно с помощью [forced_compaction]({{ core-docs-root }}/{{ lang }}/user-guide/explanation/dynamic-tables/overview{{ docs-revision-query }}#attributes), однако использование этого метода создаёт большую нагрузку на кластер и крайне не рекомендуется.

Для проверки того, что таблица действительно изменила медиум можно воспользоваться командой:

```bash
$ yt get //home/dev/test_table/@chunk_media_statistics

{
    "ssd_blobs" = {
        "chunk_count" = 2126;
        "uncompressed_data_size" = 9667220402266;
        "compressed_data_size" = 4954465956017;
        "data_weight" = 10764306825793;
        "max_block_size" = 6584787;
    };
}
```

------

#### **Q: Поддерживается ли конструкция SAMPLE языка ClickHouse?** {#chyt-sample-support}

**A:** CHYT поддерживает конструкцию Sample. Отличие заключается в том, что CHYT игнорирует команду `OFFSET ...`, таким образом нельзя получить выборку из другой части отобранных данных.

  Пример:

  ```SQL
  SELECT count(*) FROM "//tmp/sample_table" SAMPLE 0.05;

  SELECT count(*) FROM "//tmp/sample_table" SAMPLE 1/20;

  SELECT count(*) FROM "//tmp/sample_table" SAMPLE 100500;
  ```

------

#### **Q: Как мне получить имя таблицы в запросе?** {#get-table-name-query}

**A:** Можно воспользоваться виртуальными колонками `$table_name` и `$table_path`. Подробнее про виртуальные колонки читайте в разделе [Работа с таблицами {{product-name}}]({{ docs_root }}/chyt/user-guide/reference/yt-tables.md#virtual_columns).
