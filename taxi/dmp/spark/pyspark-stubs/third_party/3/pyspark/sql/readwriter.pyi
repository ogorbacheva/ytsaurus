# Stubs for pyspark.sql.readwriter (Python 3.5)
#

from typing import overload
from typing import Any, Dict, List, Optional, Union

from pyspark.sql.dataframe import DataFrame
from pyspark.rdd import RDD
from pyspark.sql.context import SQLContext
from pyspark.sql.types import *

PathOrPaths = Union[str, List[str]]

class OptionUtils: ...

class DataFrameReader(OptionUtils):
    def __init__(self, spark: SQLContext) -> None: ...
    def format(self, source: str) -> 'DataFrameReader': ...
    def schema(self, schema: StringType) -> 'DataFrameReader': ...
    def option(self, key: str, value: str) -> 'DataFrameReader': ...
    def options(self, **options: str) -> 'DataFrameReader': ...
    def load(self, path: Optional[PathOrPaths] = ..., format: Optional[str] = ..., schema: Optional[StructType] = ..., **options: str) -> DataFrame: ...
    def json(self, path: Union[str, List[str], RDD[str]], schema: Optional[StructType] = ..., primitivesAsString: Optional[Union[bool, str]] = ..., prefersDecimal: Optional[Union[bool, str]] = ..., allowComments: Optional[Union[bool, str]] = ..., allowUnquotedFieldNames: Optional[Union[bool, str]] = ..., allowSingleQuotes: Optional[Union[bool, str]] = ..., allowNumericLeadingZero: Optional[Union[bool, str]] = ..., allowBackslashEscapingAnyCharacter: Optional[Union[bool, str]] = ..., mode: Optional[str] = ..., columnNameOfCorruptRecord: Optional[str] = ..., dateFormat: Optional[str] = ..., timestampFormat: Optional[str] = ...) -> DataFrame: ...
    def table(self, tableName: str) -> DataFrame: ...
    def parquet(self, *paths: str) -> DataFrame: ...
    def text(self, paths: PathOrPaths) -> DataFrame: ...
    def csv(self, path: PathOrPaths, schema: Optional[StructType] = ..., sep: Optional[str] = ..., encoding: Optional[str] = ..., quote: Optional[str] = ..., escape: Optional[str] = ..., comment: Optional[str] = ..., header: Optional[Union[bool, str]] = ..., inferSchema: Optional[Union[bool, str]] = ..., ignoreLeadingWhiteSpace: Optional[Union[bool, str]] = ..., ignoreTrailingWhiteSpace: Optional[Union[bool, str]] = ..., nullValue: Optional[str] = ..., nanValue: Optional[str] = ..., positiveInf: Optional[str] = ..., negativeInf: Optional[str] = ..., dateFormat: Optional[str] = ..., timestampFormat: Optional[str] = ..., maxColumns: Optional[int] = ..., maxCharsPerColumn: Optional[int] = ..., maxMalformedLogPerPartition: Optional[int] = ..., mode: Optional[str] = ...) -> DataFrame: ...
    def orc(self, path: PathOrPaths) -> DataFrame: ...
    @overload
    def jdbc(self, url, table, *, properties: Optional[Dict[str, str]] = ...) -> DataFrame: ...
    @overload
    def jdbc(self, url, table, column: Optional[Any] = ..., lowerBound: Optional[Any] = ..., upperBound: Optional[Any] = ..., numPartitions: Optional[Any] = ..., *, properties: Optional[Dict[str, str]] = ...) -> DataFrame: ...
    @overload
    def jdbc(self, url, table, *, predicates: Optional[Any] = ..., properties: Optional[Dict[str, str]] = ...) -> DataFrame: ...

class DataFrameWriter(OptionUtils):
    def __init__(self, df) -> None: ...
    def mode(self, saveMode: str) -> 'DataFrameWriter': ...
    def format(self, source: str) -> 'DataFrameWriter': ...
    def option(self, key: str, value: str) -> 'DataFrameWriter': ...
    def options(self, **options: str) -> 'DataFrameWriter': ...
    @overload
    def partitionBy(self, *cols: str) -> 'DataFrameWriter': ...
    @overload
    def partitionBy(self, __cols: List[str]) -> 'DataFrameWriter': ...
    def save(self, path: Optional[str] = ..., format: Optional[str] = ..., mode: Optional[str] = ..., partitionBy: Optional[List[str]] = ..., **options: str) -> None: ...
    def insertInto(self, tableName: str, overwrite: bool = ...) -> None: ...
    def saveAsTable(self, name: str, format: Optional[str] = ..., mode: Optional[str] = ..., partitionBy: Optional[List[str]] = ..., **options: str) -> None: ...
    def json(self, path: str, mode: Optional[str] = ..., compression: Optional[str] = ..., dateFormat: Optional[str] = ..., timestampFormat: Optional[str] = ...) -> None: ...
    def parquet(self, path: str, mode: Optional[str] = ..., partitionBy: Optional[List[str]] = ..., compression: Optional[str] = ...) -> None: ...
    def text(self, path: str, compression: Optional[str] = ...) -> None: ...
    def csv(self, path: str, mode: Optional[str] = ..., compression: Optional[str] = ..., sep: Optional[str] = ..., quote: Optional[str] = ..., escape: Optional[str] = ..., header: Optional[Union[bool, str]] = ..., nullValue: Optional[str] = ..., escapeQuotes: Optional[Union[bool, str]] = ..., quoteAll: Optional[Union[bool, str]] = ..., dateFormat: Optional[str] = ..., timestampFormat: Optional[str] = ...) -> None: ...
    def orc(self, path: str, mode: Optional[str] = ..., partitionBy: Optional[List[str]] = ..., compression: Optional[str] = ...) -> None: ...
    def jdbc(self, url: str, table: str, mode: Optional[str] = ..., properties: Optional[Dict[str, str]] = ...) -> None: ...

