# Converting a dynamic table into a static table

1. Create a static table with the same schema and settings as the original dynamic table.
2. Run the `Merge` operation on top of the dynamic table with output to the static table.

To implement the above steps, use the script indicated in the [MapReduce for dynamic tables]({{ docs_root }}/core/how-to-guides/dynamic-tables/mapreduce) section.
