from typing import Any, Dict, TypeVar, Union

import pyspark.ml.param
import pyspark.ml.base
import pyspark.ml.util

ParamMap = Dict[pyspark.ml.param.Param, Any]
PipelineStage = Union[pyspark.ml.base.Estimator, pyspark.ml.base.Transformer]

T = TypeVar("T")
P = TypeVar("P", bound=pyspark.ml.param.Params)
M = TypeVar("M", bound=pyspark.ml.base.Transformer)
