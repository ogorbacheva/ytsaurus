# Stubs for pyspark.ml.pipeline (Python 3.5)
#
# NOTE: This dynamically typed stub was automatically generated by stubgen.

from typing import Any, Optional
from pyspark.ml.base import Estimator, Model
from pyspark.ml.util import MLReadable, MLWritable

basestring = ...  # type: Any

class Pipeline(Estimator, MLReadable, MLWritable):
    stages = ...  # type: Any
    def __init__(self, stages: Optional[Any] = ...) -> None: ...
    def setStages(self, value): ...
    def getStages(self): ...
    def setParams(self, stages: Optional[Any] = ...): ...
    def copy(self, extra: Optional[Any] = ...): ...
    def write(self): ...
    def save(self, path): ...
    @classmethod
    def read(cls): ...

class PipelineModel(Model, MLReadable, MLWritable):
    stages = ...  # type: Any
    def __init__(self, stages) -> None: ...
    def copy(self, extra: Optional[Any] = ...): ...
    def write(self): ...
    def save(self, path): ...
    @classmethod
    def read(cls): ...
