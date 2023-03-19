#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

from typing import overload
from typing import (
    Any,
    Callable,
    Dict,
    Iterator,
    List,
    Optional,
    Tuple,
    Union,
)

from py4j.java_gateway import JavaObject  # type: ignore[import]

from pyspark.sql._typing import ColumnOrName, LiteralType, OptionalPrimitiveType
from pyspark._typing import PrimitiveType
from pyspark.sql.types import (  # noqa: F401
    StructType,
    StructField,
    StringType,
    IntegerType,
    Row,
)  # noqa: F401
from pyspark.sql.context import SQLContext
from pyspark.sql.group import GroupedData
from pyspark.sql.readwriter import DataFrameWriter, DataFrameWriterV2
from pyspark.sql.streaming import DataStreamWriter
from pyspark.sql.column import Column
from pyspark.rdd import RDD
from pyspark.storagelevel import StorageLevel

from pyspark.sql.pandas.conversion import PandasConversionMixin
from pyspark.sql.pandas.map_ops import PandasMapOpsMixin
from pyspark.pandas.frame import DataFrame as PandasOnSparkDataFrame

class DataFrame(PandasMapOpsMixin, PandasConversionMixin):
    sql_ctx: SQLContext
    is_cached: bool
    def __init__(self, jdf: JavaObject, sql_ctx: SQLContext) -> None: ...
    @property
    def rdd(self) -> RDD[Row]: ...
    @property
    def na(self) -> DataFrameNaFunctions: ...
    @property
    def stat(self) -> DataFrameStatFunctions: ...
    def toJSON(self, use_unicode: bool = ...) -> RDD[str]: ...
    def registerTempTable(self, name: str) -> None: ...
    def createTempView(self, name: str) -> None: ...
    def createOrReplaceTempView(self, name: str) -> None: ...
    def createGlobalTempView(self, name: str) -> None: ...
    @property
    def write(self) -> DataFrameWriter: ...
    @property
    def writeStream(self) -> DataStreamWriter: ...
    @property
    def schema(self) -> StructType: ...
    def printSchema(self) -> None: ...
    def explain(
        self, extended: Optional[Union[bool, str]] = ..., mode: Optional[str] = ...
    ) -> None: ...
    def exceptAll(self, other: DataFrame) -> DataFrame: ...
    def isLocal(self) -> bool: ...
    @property
    def isStreaming(self) -> bool: ...
    def show(
        self, n: int = ..., truncate: Union[bool, int] = ..., vertical: bool = ...
    ) -> None: ...
    def checkpoint(self, eager: bool = ...) -> DataFrame: ...
    def localCheckpoint(self, eager: bool = ...) -> DataFrame: ...
    def withWatermark(
        self, eventTime: str, delayThreshold: str
    ) -> DataFrame: ...
    def hint(self, name: str, *parameters: Union[PrimitiveType, List[PrimitiveType]]) -> DataFrame: ...
    def count(self) -> int: ...
    def collect(self) -> List[Row]: ...
    def toLocalIterator(self, prefetchPartitions: bool = ...) -> Iterator[Row]: ...
    def limit(self, num: int) -> DataFrame: ...
    def take(self, num: int) -> List[Row]: ...
    def tail(self, num: int) -> List[Row]: ...
    def foreach(self, f: Callable[[Row], None]) -> None: ...
    def foreachPartition(self, f: Callable[[Iterator[Row]], None]) -> None: ...
    def cache(self) -> DataFrame: ...
    def persist(self, storageLevel: StorageLevel = ...) -> DataFrame: ...
    @property
    def storageLevel(self) -> StorageLevel: ...
    def unpersist(self, blocking: bool = ...) -> DataFrame: ...
    def coalesce(self, numPartitions: int) -> DataFrame: ...
    @overload
    def repartition(self, numPartitions: int, *cols: ColumnOrName) -> DataFrame: ...
    @overload
    def repartition(self, *cols: ColumnOrName) -> DataFrame: ...
    @overload
    def repartitionByRange(
        self, numPartitions: int, *cols: ColumnOrName
    ) -> DataFrame: ...
    @overload
    def repartitionByRange(self, *cols: ColumnOrName) -> DataFrame: ...
    def distinct(self) -> DataFrame: ...
    @overload
    def sample(self, fraction: float, seed: Optional[int] = ...) -> DataFrame: ...
    @overload
    def sample(
        self,
        withReplacement: Optional[bool],
        fraction: float,
        seed: Optional[int] = ...,
    ) -> DataFrame: ...
    def sampleBy(
        self, col: ColumnOrName, fractions: Dict[Any, float], seed: Optional[int] = ...
    ) -> DataFrame: ...
    def randomSplit(
        self, weights: List[float], seed: Optional[int] = ...
    ) -> List[DataFrame]: ...
    @property
    def dtypes(self) -> List[Tuple[str, str]]: ...
    @property
    def columns(self) -> List[str]: ...
    def colRegex(self, colName: str) -> Column: ...
    def alias(self, alias: str) -> DataFrame: ...
    def crossJoin(self, other: DataFrame) -> DataFrame: ...
    def join(
        self,
        other: DataFrame,
        on: Optional[Union[str, List[str], Column, List[Column]]] = ...,
        how: Optional[str] = ...,
    ) -> DataFrame: ...
    def sortWithinPartitions(
        self,
        *cols: Union[str, Column, List[Union[str, Column]]],
        ascending: Union[bool, List[bool]] = ...
    ) -> DataFrame: ...
    def sort(
        self,
        *cols: Union[str, Column, List[Union[str, Column]]],
        ascending: Union[bool, List[bool]] = ...
    ) -> DataFrame: ...
    def orderBy(
        self,
        *cols: Union[str, Column, List[Union[str, Column]]],
        ascending: Union[bool, List[bool]] = ...
    ) -> DataFrame: ...
    def describe(self, *cols: Union[str, List[str]]) -> DataFrame: ...
    def summary(self, *statistics: str) -> DataFrame: ...
    @overload
    def head(self) -> Row: ...
    @overload
    def head(self, n: int) -> List[Row]: ...
    def first(self) -> Row: ...
    def __getitem__(self, item: Union[int, str, Column, List, Tuple]) -> Column: ...
    def __getattr__(self, name: str) -> Column: ...
    @overload
    def select(self, *cols: ColumnOrName) -> DataFrame: ...
    @overload
    def select(self, __cols: Union[List[Column], List[str]]) -> DataFrame: ...
    @overload
    def selectExpr(self, *expr: str) -> DataFrame: ...
    @overload
    def selectExpr(self, *expr: List[str]) -> DataFrame: ...
    def filter(self, condition: ColumnOrName) -> DataFrame: ...
    @overload
    def groupBy(self, *cols: ColumnOrName) -> GroupedData: ...
    @overload
    def groupBy(self, __cols: Union[List[Column], List[str]]) -> GroupedData: ...
    @overload
    def rollup(self, *cols: ColumnOrName) -> GroupedData: ...
    @overload
    def rollup(self, __cols: Union[List[Column], List[str]]) -> GroupedData: ...
    @overload
    def cube(self, *cols: ColumnOrName) -> GroupedData: ...
    @overload
    def cube(self, __cols: Union[List[Column], List[str]]) -> GroupedData: ...
    def agg(self, *exprs: Union[Column, Dict[str, str]]) -> DataFrame: ...
    def union(self, other: DataFrame) -> DataFrame: ...
    def unionAll(self, other: DataFrame) -> DataFrame: ...
    def unionByName(
        self, other: DataFrame, allowMissingColumns: bool = ...
    ) -> DataFrame: ...
    def intersect(self, other: DataFrame) -> DataFrame: ...
    def intersectAll(self, other: DataFrame) -> DataFrame: ...
    def subtract(self, other: DataFrame) -> DataFrame: ...
    def dropDuplicates(self, subset: Optional[List[str]] = ...) -> DataFrame: ...
    def dropna(
        self,
        how: str = ...,
        thresh: Optional[int] = ...,
        subset: Optional[Union[str, Tuple[str, ...], List[str]]] = ...,
    ) -> DataFrame: ...
    @overload
    def fillna(
        self,
        value: LiteralType,
        subset: Optional[Union[str, Tuple[str, ...], List[str]]] = ...,
    ) -> DataFrame: ...
    @overload
    def fillna(self, value: Dict[str, LiteralType]) -> DataFrame: ...
    @overload
    def replace(
        self,
        to_replace: LiteralType,
        value: OptionalPrimitiveType,
        subset: Optional[List[str]] = ...,
    ) -> DataFrame: ...
    @overload
    def replace(
        self,
        to_replace: List[LiteralType],
        value: List[OptionalPrimitiveType],
        subset: Optional[List[str]] = ...,
    ) -> DataFrame: ...
    @overload
    def replace(
        self,
        to_replace: Dict[LiteralType, OptionalPrimitiveType],
        subset: Optional[List[str]] = ...,
    ) -> DataFrame: ...
    @overload
    def replace(
        self,
        to_replace: List[LiteralType],
        value: OptionalPrimitiveType,
        subset: Optional[List[str]] = ...,
    ) -> DataFrame: ...
    @overload
    def approxQuantile(
        self,
        col: str,
        probabilities: Union[List[float], Tuple[float]],
        relativeError: float,
    ) -> List[float]: ...
    @overload
    def approxQuantile(
        self,
        col: Union[List[str], Tuple[str]],
        probabilities: Union[List[float], Tuple[float]],
        relativeError: float,
    ) -> List[List[float]]: ...
    def corr(self, col1: str, col2: str, method: Optional[str] = ...) -> float: ...
    def cov(self, col1: str, col2: str) -> float: ...
    def crosstab(self, col1: str, col2: str) -> DataFrame: ...
    def freqItems(
        self, cols: Union[List[str], Tuple[str]], support: Optional[float] = ...
    ) -> DataFrame: ...
    def withColumn(self, colName: str, col: Column) -> DataFrame: ...
    def withColumnRenamed(self, existing: str, new: str) -> DataFrame: ...
    @overload
    def drop(self, cols: ColumnOrName) -> DataFrame: ...
    @overload
    def drop(self, *cols: str) -> DataFrame: ...
    def toDF(self, *cols: ColumnOrName) -> DataFrame: ...
    def transform(self, func: Callable[[DataFrame], DataFrame]) -> DataFrame: ...
    @overload
    def groupby(self, *cols: ColumnOrName) -> GroupedData: ...
    @overload
    def groupby(self, __cols: Union[List[Column], List[str]]) -> GroupedData: ...
    def drop_duplicates(self, subset: Optional[List[str]] = ...) -> DataFrame: ...
    def where(self, condition: ColumnOrName) -> DataFrame: ...
    def sameSemantics(self, other: DataFrame) -> bool: ...
    def semanticHash(self) -> int: ...
    def inputFiles(self) -> List[str]: ...
    def writeTo(self, table: str) -> DataFrameWriterV2: ...
    def to_pandas_on_spark(self, index_col: Optional[Union[str, List[str]]] = None) -> PandasOnSparkDataFrame: ...

class DataFrameNaFunctions:
    df: DataFrame
    def __init__(self, df: DataFrame) -> None: ...
    def drop(
        self,
        how: str = ...,
        thresh: Optional[int] = ...,
        subset: Optional[List[str]] = ...,
    ) -> DataFrame: ...
    @overload
    def fill(
        self, value: LiteralType, subset: Optional[List[str]] = ...
    ) -> DataFrame: ...
    @overload
    def fill(self, value: Dict[str, LiteralType]) -> DataFrame: ...
    @overload
    def replace(
        self,
        to_replace: LiteralType,
        value: OptionalPrimitiveType,
        subset: Optional[List[str]] = ...,
    ) -> DataFrame: ...
    @overload
    def replace(
        self,
        to_replace: List[LiteralType],
        value: List[OptionalPrimitiveType],
        subset: Optional[List[str]] = ...,
    ) -> DataFrame: ...
    @overload
    def replace(
        self,
        to_replace: Dict[LiteralType, OptionalPrimitiveType],
        subset: Optional[List[str]] = ...,
    ) -> DataFrame: ...
    @overload
    def replace(
        self,
        to_replace: List[LiteralType],
        value: OptionalPrimitiveType,
        subset: Optional[List[str]] = ...,
    ) -> DataFrame: ...

class DataFrameStatFunctions:
    df: DataFrame
    def __init__(self, df: DataFrame) -> None: ...
    @overload
    def approxQuantile(
        self,
        col: str,
        probabilities: Union[List[float], Tuple[float]],
        relativeError: float,
    ) -> List[float]: ...
    @overload
    def approxQuantile(
        self,
        col: Union[List[str], Tuple[str]],
        probabilities: Union[List[float], Tuple[float]],
        relativeError: float,
    ) -> List[List[float]]: ...
    def corr(self, col1: str, col2: str, method: Optional[str] = ...) -> float: ...
    def cov(self, col1: str, col2: str) -> float: ...
    def crosstab(self, col1: str, col2: str) -> DataFrame: ...
    def freqItems(
        self, cols: List[str], support: Optional[float] = ...
    ) -> DataFrame: ...
    def sampleBy(
        self, col: str, fractions: Dict[Any, float], seed: Optional[int] = ...
    ) -> DataFrame: ...
