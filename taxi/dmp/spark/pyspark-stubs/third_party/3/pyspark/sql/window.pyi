# Stubs for pyspark.sql.window (Python 3.5)
#

from typing import Any, Union
from pyspark.sql._typing import ColumnOrName
from py4j.java_gateway import JavaObject # type: ignore

class Window:
    unboundedPreceding = ...  # type: int
    unboundedFollowing = ...  # type: int
    currentRow = ...  # type: int
    @staticmethod
    def partitionBy(*cols: ColumnOrName) -> WindowSpec: ...
    @staticmethod
    def orderBy(*cols: ColumnOrName) -> WindowSpec: ...
    @staticmethod
    def rowsBetween(start: int, end: int) -> WindowSpec: ...
    @staticmethod
    def rangeBetween(start: int, end: int) -> WindowSpec: ...

class WindowSpec:
    def __init__(self, jspec: JavaObject) -> None: ...
    def partitionBy(self, *cols: ColumnOrName) -> 'WindowSpec': ...
    def orderBy(self, *cols: ColumnOrName) -> 'WindowSpec': ...
    def rowsBetween(self, start: int, end: int) -> 'WindowSpec': ...
    def rangeBetween(self, start: int, end: int) -> 'WindowSpec': ...
