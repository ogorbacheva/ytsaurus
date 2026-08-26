
# Опции записи

## sorted_by

Указание сортировки по некоторому префиксу колонок:

```python
df.write.sorted_by("uuid").yt("//sys/spark/examples/test_data")
```

## unique_keys

Уникальность ключа в таблице:

```python
df.write.sorted_by("uuid").unique_keys.yt("//sys/spark/examples/test_data")
```

## optimize_for

Таблица может храниться в построчном (lookup) и поколоночном (scan) формате. Предпочитаемый выбирается в зависимости от задачи:

```python
spark.write.optimize_for("scan").yt("//sys/spark/examples/test_data")
spark.write.optimize_for("lookup").yt("//sys/spark/examples/test_data")
```

## Schema v3

Запись таблиц со схемой в формате [type_v3]({{ core-docs-root }}/{{ lang }}/reference/storage/data-types{{ docs-revision-query }}) вместо `type_v1`. Настраивается в [Spark конфигурации]({{ spyt-docs-root }}/{{ lang }}/how-to-guides/cluster/configure{{ docs-revision-query }}) или опции записи.

Python example:
```python
df.write.option("write_type_v3", "true")
```

## Динамические таблицы

Для динамических таблиц необходимо явно указать дополнительную опцию `inconsistent_dynamic_write` со значением `true`, чтобы подтвердить, что вы согласны с отсутствием поддержки транзакционной записи в динамические таблицы.

Python example:
```python
df.write.option("inconsistent_dynamic_write", "true")
```
