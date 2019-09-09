# Stubs for pyspark.sql.context (Python 3.5)
#

from typing import overload
from typing import Any, Callable, List, Optional, Tuple, TypeVar, Union

import pandas.core.frame # type: ignore
from py4j.java_gateway import JavaObject  # type: ignore

from pyspark.sql._typing import DateTimeLiteral, LiteralType, DecimalLiteral, DataTypeOrString
from pyspark.context import SparkContext
from pyspark.rdd import RDD
from pyspark.sql.dataframe import DataFrame
from pyspark.sql.session import SparkSession
from pyspark.sql.types import DataType, StructType, Row
from pyspark.sql.udf import UDFRegistration as UDFRegistration
from pyspark.sql.readwriter import DataFrameReader
from pyspark.sql.streaming import DataStreamReader, StreamingQueryManager

T = TypeVar('T')

class SQLContext:
    sparkSession: SparkSession
    def __init__(self, sparkContext, sparkSession: Optional[SparkSession] = ..., jsqlContext: Optional[JavaObject] = ...) -> None: ...
    @classmethod
    def getOrCreate(cls: type, sc: SparkContext) -> SQLContext:...
    def newSession(self) -> SQLContext: ...
    def setConf(self, key: str, value) -> None: ...
    def getConf(self, key: str, defaultValue: Optional[str] = ...) -> str: ...
    @property
    def udf(self) -> UDFRegistration: ...
    def range(self, start: int, end: Optional[int] = ..., step: int = ..., numPartitions: Optional[int] = ...) -> DataFrame: ...
    def registerFunction(self, name: str, f: Callable[..., Any], returnType: DataType = ...) -> None: ...
    def registerJavaFunction(self, name: str, javaClassName: str, returnType: Optional[DataType] = ...) -> None: ...
    @overload
    def createDataFrame(self, data: Union[RDD[Union[Tuple, List]], List[Union[Tuple, List]], pandas.core.frame.DataFrame], samplingRatio: Optional[float] = ...) -> DataFrame: ...
    @overload
    def createDataFrame(self, data: Union[RDD[Union[Tuple, List]], List[Union[Tuple, List]], pandas.core.frame.DataFrame], schema: Optional[Union[List[str], Tuple[str, ...]]] = ..., samplingRatio: Optional[float] = ...) -> DataFrame: ...
    @overload
    def createDataFrame(self, data: Union[RDD[Union[DateTimeLiteral, LiteralType, DecimalLiteral]], List[Union[DateTimeLiteral, LiteralType, DecimalLiteral]]], schema: DataType, verifySchema: bool = ...) -> DataFrame: ...
    def registerDataFrameAsTable(self, df: DataFrame, tableName: str) -> None: ...
    def dropTempTable(self, tableName: str) -> None: ...
    def sql(self, sqlQuery: str) -> DataFrame: ...
    def table(self, tableName: str) -> DataFrame: ...
    def tables(self, dbName: Optional[str] = ...) -> DataFrame: ...
    def tableNames(self, dbName: Optional[str] = ...) -> List[str]: ...
    def cacheTable(self, tableName: str) -> None: ...
    def uncacheTable(self, tableName: str) -> None: ...
    def clearCache(self) -> None: ...
    @property
    def read(self) -> DataFrameReader: ...
    @property
    def readStream(self) -> DataStreamReader: ...
    @property
    def streams(self) -> StreamingQueryManager: ...
