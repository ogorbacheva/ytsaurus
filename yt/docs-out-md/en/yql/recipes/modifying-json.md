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
vcsPath: en/yql/recipes/modifying-json.md
sourcePath: en/yql/recipes/modifying-json.md
---
# Modifying JSON with YQL

In memory, YQL works with immutable values. When a query needs to change something inside a JSON value, think of it as constructing a new value from parts of the old one.

This sample query takes an input JSON named `$fields`, parses it, substitutes the `a` key with 0, deletes the `d` key, and adds the `c` key with value 3:

```yql
$fields = '{"a": 1, "b": 2, "d": 4}'j;
$pairs = DictItems(Yson::ConvertToInt64Dict($fields));
$result_pairs = ListExtend(ListNotNull(ListMap($pairs, ($item) -> {
    $item = if ($item.0 == "a", ("a", 0), $item);
    return if ($item.0 == "d", null, $item);
})), [("c", 3)]);
$result_dict = ToDict($result_pairs);
SELECT Yson::SerializeJson(Yson::From($result_dict));
```

## See also

- [{#T}](../udf/list/yson.md)
- [{#T}](../builtins/list.md)
- [{#T}](../builtins/dict.md)
- [{#T}](accessing-json.md)
