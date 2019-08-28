# Stubs for pyspark.sql.group (Python 3.5)
#

from typing import overload
from typing import Any, Callable, Dict, List, Optional

from pyspark.sql._typing import LiteralType, GroupedMapPandasUserDefinedFunction
from pyspark.sql.context import SQLContext
from pyspark.sql.column import Column
from pyspark.sql.dataframe import DataFrame
from pyspark.sql.types import *
from py4j.java_gateway import JavaObject  # type: ignore

class GroupedData:
    sql_ctx: SQLContext
    def __init__(self, jgd: JavaObject, df: DataFrame) -> None: ...
    @overload
    def agg(self, *exprs: Column) -> DataFrame: ...
    @overload
    def agg(self, __exprs: Dict[str, str]) -> DataFrame: ...
    def count(self) -> DataFrame: ...
    def mean(self, *cols: str) -> DataFrame: ...
    def avg(self, *cols: str) -> DataFrame: ...
    def max(self, *cols: str) -> DataFrame: ...
    def min(self, *cols: str) -> DataFrame: ...
    def sum(self, *cols: str) -> DataFrame: ...
    def pivot(self, pivot_col: str, values: Optional[List[LiteralType]] = ...) -> GroupedData: ...
    def apply(self, udf: GroupedMapPandasUserDefinedFunction) -> DataFrame: ...
