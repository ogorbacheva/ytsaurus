---
metadata:
  - name: generator
    content: Diplodoc Platform v5.5.3
csp:
  - script-src-elem:
      - https://mc.yandex.ru
  - connect-src:
      - https://*.algolia.net
      - https://*.algolianet.com
vcsPath: ru/yql/udf/list/dsv.md
sourcePath: ru/yql/udf/list/dsv.md
---
# Dsv UDF

Функции для преобразования строк вида `"key1=value1\tkey2=value2"` в словари.

``` yql
Dsv::ReadRecord(Struct<key:String,subkey:String,value:String>) -> Struct<key:String,subkey:String,dict:Dict<String,String>>
Dsv::Parse(String,[String]) -> Dict<String,String> -- второй аргумент определяет разделитель, по умолчанию — табуляция
```

#### Пример

``` yql
SELECT Dsv::Parse("a=b@@c=d@@e=f", "@@")["c"]; -- "d"
```
