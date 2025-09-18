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
vcsPath: en/yql/udf/list/dsv.md
sourcePath: en/yql/udf/list/dsv.md
---
# Dsv UDF

Functions that convert strings formatted as `"key1=value1\tkey2=value2"` to dictionaries.

```yql
Dsv::ReadRecord(Struct<key:String,subkey:String,value:String>) -> Struct<key:String,subkey:String,dict:Dict<String,String>>
Dsv::Parse(String,[String]) -> Dict<String,String> -- the second argument defines the delimiter (tab by default)
```

#### Example

```yql
SELECT Dsv::Parse("a=b@@c=d@@e=f", "@@")["c"]; -- "d"
```
