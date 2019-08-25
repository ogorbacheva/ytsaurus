# Stubs for pyspark.ml.param.shared (Python 3.5)
#

from typing import Any, Generic, List, TypeVar
from pyspark.ml.param import *

T = TypeVar("T")

class HasMaxIter(Params):
    maxIter: Param
    def __init__(self) -> None: ...
    def setMaxIter(self: T, value: int) -> T: ...
    def getMaxIter(self) -> int: ...

class HasRegParam(Params):
    regParam: Param
    def __init__(self) -> None: ...
    def setRegParam(self: T, value: float) -> T: ...
    def getRegParam(self) -> float: ...

class HasFeaturesCol(Params):
    featuresCol: Param
    def __init__(self) -> None: ...
    def setFeaturesCol(self: T, value: str) -> T: ...
    def getFeaturesCol(self) -> str: ...

class HasLabelCol(Params):
    labelCol: Param
    def __init__(self) -> None: ...
    def setLabelCol(self: T, value: str) -> T: ...
    def getLabelCol(self) -> str: ...

class HasPredictionCol(Params):
    predictionCol: Param
    def __init__(self) -> None: ...
    def setPredictionCol(self: T, value: str) -> T: ...
    def getPredictionCol(self) -> str: ...

class HasProbabilityCol(Params):
    probabilityCol: Param
    def __init__(self) -> None: ...
    def setProbabilityCol(self: T, value: str) -> T: ...
    def getProbabilityCol(self) -> str: ...

class HasRawPredictionCol(Params):
    rawPredictionCol: Param
    def __init__(self) -> None: ...
    def setRawPredictionCol(self: T, value: str) -> T: ...
    def getRawPredictionCol(self) -> str: ...

class HasInputCol(Params):
    inputCol: Param
    def __init__(self) -> None: ...
    def setInputCol(self: T, value: str) -> T: ...
    def getInputCol(self) -> str: ...

class HasInputCols(Params):
    inputCols: Param
    def __init__(self) -> None: ...
    def setInputCols(self: T, value: List[str]) -> T: ...
    def getInputCols(self) -> List[str]: ...

class HasOutputCol(Params):
    outputCol: Param
    def __init__(self) -> None: ...
    def setOutputCol(self: T, value: str) -> T: ...
    def getOutputCol(self) -> str: ...

class HasOutputCols(Params):
    outputCols: Param
    def __init__(self) -> None: ...
    def setOutputCols(self: T, value: List[str]) -> T: ...
    def getOutputCols(self) -> List[str]: ...

class HasNumFeatures(Params):
    numFeatures: Param
    def __init__(self) -> None: ...
    def setNumFeatures(self: T, value: int) -> T: ...
    def getNumFeatures(self) -> int: ...

class HasCheckpointInterval(Params):
    checkpointInterval: Param
    def __init__(self) -> None: ...
    def setCheckpointInterval(self: T, value: int) -> T: ...
    def getCheckpointInterval(self) -> int: ...

class HasSeed(Params):
    seed: Param
    def __init__(self) -> None: ...
    def setSeed(self: T, value: int) -> T: ...
    def getSeed(self) -> int: ...

class HasTol(Params):
    tol: Param
    def __init__(self) -> None: ...
    def setTol(self: T, value: float) -> T: ...
    def getTol(self) -> float: ...

class HasStepSize(Params):
    stepSize: Param
    def __init__(self) -> None: ...
    def setStepSize(self: T, value: float) -> T: ...
    def getStepSize(self) -> float: ...

class HasHandleInvalid(Params):
    handleInvalid: Param
    def __init__(self) -> None: ...
    def setHandleInvalid(self: T, value: str) -> T: ...
    def getHandleInvalid(self) -> str: ...

class HasElasticNetParam(Params):
    elasticNetParam: Param
    def __init__(self) -> None: ...
    def setElasticNetParam(self: T, value: float) -> T: ...
    def getElasticNetParam(self) -> float: ...

class HasFitIntercept(Params):
    fitIntercept: Param
    def __init__(self) -> None: ...
    def setFitIntercept(self: T, value: bool) -> T: ...
    def getFitIntercept(self) -> bool: ...

class HasStandardization(Params):
    standardization: Param
    def __init__(self) -> None: ...
    def setStandardization(self: T, value: bool) -> T: ...
    def getStandardization(self) -> bool: ...

class HasThresholds(Params):
    thresholds: Param
    def __init__(self) -> None: ...
    def setThresholds(self: T, value: List[float]) -> T: ...
    def getThresholds(self) -> List[float]: ...

class HasThreshold(Params):
    threshold: Param
    def __init__(self) -> None: ...
    def setThreshold(self: T, value: float) -> T: ...
    def getThreshold(self) -> float: ...

class HasWeightCol(Params):
    weightCol: Param
    def __init__(self) -> None: ...
    def setWeightCol(self: T, value: str) -> T: ...
    def getWeightCol(self) -> str: ...

class HasSolver(Params):
    solver: Param
    def __init__(self) -> None: ...
    def setSolver(self: T, value: str) -> T: ...
    def getSolver(self) -> str: ...

class HasVarianceCol(Params):
    varianceCol: Param
    def __init__(self) -> None: ...
    def setVarianceCol(self: T, value: str) -> T: ...
    def getVarianceCol(self) -> str: ...

class HasAggregationDepth(Params):
    aggregationDepth: Param
    def __init__(self) -> None: ...
    def setAggregationDepth(self: T, value: int) -> T: ...
    def getAggregationDepth(self) -> int: ...

class HasParallelism(Params):
    parallelism: Param
    def __init__(self) -> None: ...
    def setParallelism(self: T, value: int) -> T: ...
    def getParallelism(self) -> int: ...

class HasCollectSubModels(Params):
    collectSubModels: Param
    def __init__(self) -> None: ...
    def setCollectSubModels(self: T, value: bool) -> T: ...
    def getCollectSubModels(self) -> bool: ...

class HasLoss(Params):
    loss: Param
    def __init__(self) -> None: ...
    def setLoss(self: T, value: str) -> T: ...
    def getLoss(self) -> str: ...

class HasValidationIndicatorCol(Params):
    validationIndicatorCol: Param
    def __init__(self) -> None: ...
    def setValidationIndicatorCol(self, value: str) -> HasValidationIndicatorCol: ...
    def getValidationIndicatorCol(self) -> str: ...

class HasDistanceMeasure(Params):
    distanceMeasure: Param
    def __init__(self) -> None: ...
    def setDistanceMeasure(self: T, value: str) -> T: ...
    def getDistanceMeasure(self) -> str: ...
