# Stubs for pyspark.sql.streaming (Python 3.5)
#
# NOTE: This dynamically typed stub was automatically generated by stubgen.

from typing import Any, Optional
from pyspark.sql.readwriter import OptionUtils
from pyspark.sql.types import *

class StreamingQuery:
    def __init__(self, jsq) -> None: ...
    @property
    def id(self): ...
    @property
    def runId(self): ...
    @property
    def name(self): ...
    @property
    def isActive(self): ...
    def awaitTermination(self, timeout: Optional[Any] = ...): ...
    @property
    def status(self): ...
    @property
    def recentProgress(self): ...
    @property
    def lastProgress(self): ...
    def processAllAvailable(self): ...
    def stop(self): ...
    def explain(self, extended: bool = ...): ...
    def exception(self): ...

class StreamingQueryManager:
    def __init__(self, jsqm) -> None: ...
    @property
    def active(self): ...
    def get(self, id): ...
    def awaitAnyTermination(self, timeout: Optional[Any] = ...): ...
    def resetTerminated(self): ...

class Trigger:
    __metaclass__ = ...  # type: Any

class ProcessingTime(Trigger):
    interval = ...  # type: Any
    def __init__(self, interval) -> None: ...

class DataStreamReader(OptionUtils):
    def __init__(self, spark) -> None: ...
    def format(self, source): ...
    def schema(self, schema): ...
    def option(self, key, value): ...
    def options(self, **options): ...
    def load(self, path: Optional[Any] = ..., format: Optional[Any] = ..., schema: Optional[Any] = ..., **options): ...
    def json(self, path, schema: Optional[Any] = ..., primitivesAsString: Optional[Any] = ..., prefersDecimal: Optional[Any] = ..., allowComments: Optional[Any] = ..., allowUnquotedFieldNames: Optional[Any] = ..., allowSingleQuotes: Optional[Any] = ..., allowNumericLeadingZero: Optional[Any] = ..., allowBackslashEscapingAnyCharacter: Optional[Any] = ..., mode: Optional[Any] = ..., columnNameOfCorruptRecord: Optional[Any] = ..., dateFormat: Optional[Any] = ..., timestampFormat: Optional[Any] = ...): ...
    def parquet(self, path): ...
    def text(self, path): ...
    def csv(self, path, schema: Optional[Any] = ..., sep: Optional[Any] = ..., encoding: Optional[Any] = ..., quote: Optional[Any] = ..., escape: Optional[Any] = ..., comment: Optional[Any] = ..., header: Optional[Any] = ..., inferSchema: Optional[Any] = ..., ignoreLeadingWhiteSpace: Optional[Any] = ..., ignoreTrailingWhiteSpace: Optional[Any] = ..., nullValue: Optional[Any] = ..., nanValue: Optional[Any] = ..., positiveInf: Optional[Any] = ..., negativeInf: Optional[Any] = ..., dateFormat: Optional[Any] = ..., timestampFormat: Optional[Any] = ..., maxColumns: Optional[Any] = ..., maxCharsPerColumn: Optional[Any] = ..., maxMalformedLogPerPartition: Optional[Any] = ..., mode: Optional[Any] = ...): ...

class DataStreamWriter:
    def __init__(self, df) -> None: ...
    def outputMode(self, outputMode): ...
    def format(self, source): ...
    def option(self, key, value): ...
    def options(self, **options): ...
    def partitionBy(self, *cols): ...
    def queryName(self, queryName): ...
    def trigger(self, processingTime: Optional[Any] = ...): ...
    def start(self, path: Optional[Any] = ..., format: Optional[Any] = ..., partitionBy: Optional[Any] = ..., queryName: Optional[Any] = ..., **options): ...
