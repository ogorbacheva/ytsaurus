# Stubs for pyspark.sql.streaming (Python 3.5)
#

from typing import overload
from typing import Any, Dict, List, Optional, Union
from pyspark.sql.context import SQLContext
from pyspark.sql.dataframe import DataFrame
from pyspark.sql.readwriter import OptionUtils
from pyspark.sql.types import StructType
from pyspark.sql.utils import StreamingQueryException

class StreamingQuery:
    def __init__(self, jsq) -> None: ...
    @property
    def id(self) -> str: ...
    @property
    def runId(self) -> str: ...
    @property
    def name(self) -> str: ...
    @property
    def isActive(self) -> bool: ...
    def awaitTermination(self, timeout: Optional[int] = ...) -> Optional[bool]: ...
    @property
    def status(self) -> Dict[str, Any]: ...
    @property
    def recentProgress(self) -> List[Dict[str, Any]]: ...
    @property
    def lastProgress(self) -> Optional[Dict[str, Any]]: ...
    def processAllAvailable(self) -> None: ...
    def stop(self) -> None: ...
    def explain(self, extended: bool = ...) -> None: ...
    def exception(self) -> Optional[StreamingQueryException]: ...

class StreamingQueryManager:
    def __init__(self, jsqm) -> None: ...
    @property
    def active(self): ...
    def get(self, id): ...
    def awaitAnyTermination(self, timeout: Optional[Any] = ...): ...
    def resetTerminated(self): ...

class DataStreamReader(OptionUtils):
    def __init__(self, spark: SQLContext) -> None: ...
    def format(self, source: str) -> 'DataStreamReader': ...
    def schema(self, schema: StructType): ...
    def option(self, key: str, value: str): ...
    def options(self, **options: str): ...
    def load(self, path: Optional[str] = ..., format: Optional[str] = ..., schema: Optional[StructType] = ..., **options: str) -> DataFrame: ...
    def json(self, path, schema: Optional[str] = ..., primitivesAsString: Optional[Union[bool, str]] = ..., prefersDecimal: Optional[Union[bool, str]] = ..., allowComments: Optional[Union[bool, str]] = ..., allowUnquotedFieldNames: Optional[Union[bool, str]] = ..., allowSingleQuotes: Optional[Union[bool, str]] = ..., allowNumericLeadingZero: Optional[Union[bool, str]] = ..., allowBackslashEscapingAnyCharacter: Optional[Union[bool, str]] = ..., mode: Optional[str] = ..., columnNameOfCorruptRecord: Optional[str] = ..., dateFormat: Optional[str] = ..., timestampFormat: Optional[str] = ..., multiLine: Optional[Union[bool, str]] = ..., allowUnquotedControlChars: Optional[Union[bool, str]] = ..., lineSep: Optional[str] = ...) -> DataFrame: ...
    def orc(self, path: str) -> DataFrame: ...
    def parquet(self, path: str) -> DataFrame: ...
    def text(self, path: str) -> DataFrame: ...
    def csv(self, path: str, schema: Optional[StructType] = ..., sep: Optional[str] = ..., encoding: Optional[str] = ..., quote: Optional[str] = ..., escape: Optional[str] = ..., comment: Optional[str] = ..., header: Optional[Union[bool, str]] = ..., inferSchema: Optional[Union[bool, str]] = ..., ignoreLeadingWhiteSpace: Optional[Union[bool, str]] = ..., ignoreTrailingWhiteSpace: Optional[Union[bool, str]] = ..., nullValue: Optional[str] = ..., nanValue: Optional[str] = ..., positiveInf: Optional[str] = ..., negativeInf: Optional[str] = ..., dateFormat: Optional[str] = ..., timestampFormat: Optional[str] = ..., maxColumns: Optional[Union[int, str]] = ..., maxCharsPerColumn: Optional[Union[int, str]] = ..., mode: Optional[str] = ..., columnNameOfCorruptRecord: Optional[str] = ..., multiLine: Optional[Union[bool, str]] = ..., charToEscapeQuoteEscaping: Optional[Union[bool, str]] = ...) -> DataFrame: ...

class DataStreamWriter:
    def __init__(self, df: DataFrame) -> None: ...
    def outputMode(self, outputMode: str) -> 'DataStreamWriter': ...
    def format(self, source: str) -> 'DataStreamWriter': ...
    def option(self, key: str, value: str) -> 'DataStreamWriter': ...
    def options(self, **options: str) -> 'DataStreamWriter': ...
    @overload
    def partitionBy(self, *cols: str) -> 'DataStreamWriter': ...
    @overload
    def partitionBy(self, __cols: List[str]) -> 'DataStreamWriter': ...
    def queryName(self, queryName: str) -> 'DataStreamWriter': ...
    @overload
    def trigger(self, processingTime: str) -> 'DataStreamWriter': ...
    @overload
    def trigger(self, once: bool) -> 'DataStreamWriter': ...
    @overload
    def trigger(self, continuous: bool) -> 'DataStreamWriter': ...
    def start(self, path: Optional[str] = ..., format: Optional[str] = ..., outputMode: Optional[str] = ..., partitionBy: Optional[Union[str, List[str]]] = ..., queryName: Optional[str] = ..., **options: str) -> StreamingQuery: ...
