# Stubs for pyspark.ml.classification (Python 3.5)
#

from typing import Any, Dict, List, Optional
from pyspark.ml.base import Estimator, Model, Transformer
from pyspark.ml.linalg import Matrix, Vector
from pyspark.ml.param.shared import *
from pyspark.ml.regression import DecisionTreeModel, DecisionTreeRegressionModel, RandomForestParams, TreeEnsembleModel, TreeEnsembleParams
from pyspark.ml.util import *
from pyspark.ml.wrapper import JavaEstimator, JavaModel
from pyspark.ml.wrapper import JavaWrapper
from pyspark.sql.dataframe import DataFrame

ParamMap = Dict[Param, Any]

class JavaClassificationModel(JavaPredictionModel):
    @property
    def numClasses(self) -> int: ...

class LinearSVC(JavaEstimator, HasFeaturesCol, HasLabelCol, HasPredictionCol, HasMaxIter, HasRegParam, HasTol, HasRawPredictionCol, HasFitIntercept, HasStandardization, HasThreshold, HasWeightCol, HasAggregationDepth, JavaMLWritable, JavaMLReadable):
    def __init__(self, featuresCol: str = ..., labelCol: str = ..., predictionCol: str = ..., maxIter: int = ..., regParam: float = ..., tol: float = ..., rawPredictionCol: str = ..., fitIntercept: bool = ..., standardization: bool = ..., threshold: float = ..., weightCol: Optional[Any] = ..., aggregationDepth: int = ...) -> None: ...
    def setParams(self, featuresCol: str = ..., labelCol: str = ..., predictionCol: str = ..., maxIter: int = ..., regParam: float = ..., tol: float = ..., rawPredictionCol: str = ..., fitIntercept: bool = ..., standardization: bool = ..., threshold: float = ..., weightCol: Optional[Any] = ..., aggregationDepth: int = ...): ...

class LinearSVCModel(JavaModel, JavaClassificationModel, JavaMLWritable, JavaMLReadable):
    @property
    def coefficients(self): ...
    @property
    def intercept(self): ...

class LogisticRegression(JavaEstimator, HasFeaturesCol, HasLabelCol, HasPredictionCol, HasMaxIter, HasRegParam, HasTol, HasProbabilityCol, HasRawPredictionCol, HasElasticNetParam, HasFitIntercept, HasStandardization, HasThresholds, HasWeightCol, HasAggregationDepth, JavaMLWritable, JavaMLReadable):
    threshold = ...  # type: Param
    family = ...  # type: Param
    def __init__(self, featuresCol: str = ..., labelCol: str = ..., predictionCol: str = ..., maxIter: int = ..., regParam: float = ..., elasticNetParam: float = ..., tol: float = ..., fitIntercept: bool = ..., threshold: float = ..., thresholds: Optional[List[float]] = ..., probabilityCol: str = ..., rawPredictionCol: str = ..., standardization: bool = ..., weightCol: Optional[str] = ..., aggregationDepth: int = ..., family: str = ...) -> None: ...
    def setParams(self, featuresCol: str = ..., labelCol: str = ..., predictionCol: str = ..., maxIter: int = ..., regParam: float = ..., elasticNetParam: float = ..., tol: float = ..., fitIntercept: bool = ..., threshold: float = ..., thresholds: Optional[List[float]] = ..., probabilityCol: str = ..., rawPredictionCol: str = ..., standardization: bool = ..., weightCol: Optional[str] = ..., aggregationDepth: int = ..., family: str = ...) -> 'LogisticRegression': ...
    def setThreshold(self, value: float) -> 'LogisticRegression': ...
    def getThreshold(self) -> float: ...
    def setThresholds(self, value: List[float]) -> 'LogisticRegression': ...
    def getThresholds(self) -> List[float]: ...
    def setFamily(self, value: str) -> 'LogisticRegression': ...
    def getFamily(self) -> str: ...

class LogisticRegressionModel(JavaModel, JavaClassificationModel, JavaMLWritable, JavaMLReadable):
    @property
    def coefficients(self) -> Vector: ...
    @property
    def intercept(self) -> float: ...
    @property
    def coefficientMatrix(self) -> Matrix: ...
    @property
    def interceptVector(self) -> Vector: ...
    @property
    def summary(self) -> LogisticRegressionTrainingSummary: ...
    @property
    def hasSummary(self) -> bool: ...
    def evaluate(self, dataset: DataFrame) -> LogisticRegressionSummary: ...

class LogisticRegressionSummary(JavaWrapper):
    @property
    def predictions(self) -> DataFrame: ...
    @property
    def probabilityCol(self) -> str: ...
    @property
    def labelCol(self) -> str: ...
    @property
    def featuresCol(self) -> str: ...

class LogisticRegressionTrainingSummary(LogisticRegressionSummary):
    @property
    def objectiveHistory(self) -> List[float]: ...
    @property
    def totalIterations(self) -> int: ...

class BinaryLogisticRegressionSummary(LogisticRegressionSummary):
    @property
    def roc(self) -> DataFrame: ...
    @property
    def areaUnderROC(self) -> float: ...
    @property
    def pr(self) -> DataFrame: ...
    @property
    def fMeasureByThreshold(self) -> DataFrame: ...
    @property
    def precisionByThreshold(self) -> DataFrame: ...
    @property
    def recallByThreshold(self) -> DataFrame: ...

class BinaryLogisticRegressionTrainingSummary(BinaryLogisticRegressionSummary, LogisticRegressionTrainingSummary): ...

class TreeClassifierParams:
    supportedImpurities = ...  # type: List[str]
    impurity = ...  # type: Param
    def __init__(self) -> None: ...
    def setImpurity(self, value: str) -> TreeClassifierParams: ...
    def getImpurity(self)  -> str: ...

class GBTParams(TreeEnsembleParams):
    supportedLossTypes = ...  # type: List[str]

class DecisionTreeClassifier(JavaEstimator, HasFeaturesCol, HasLabelCol, HasPredictionCol, HasProbabilityCol, HasRawPredictionCol, DecisionTreeParams, TreeClassifierParams, HasCheckpointInterval, HasSeed, JavaMLWritable, JavaMLReadable):
    def __init__(self, featuresCol: str = ..., labelCol: str = ..., predictionCol: str = ..., probabilityCol: str = ..., rawPredictionCol: str = ..., maxDepth: int = ..., maxBins: int = ..., minInstancesPerNode: int = ..., minInfoGain: float = ..., maxMemoryInMB: int = ..., cacheNodeIds: bool = ..., checkpointInterval: int = ..., impurity: str = ..., seed: Optional[int] = ...) -> None: ...
    def setParams(self, featuresCol: str = ..., labelCol: str = ..., predictionCol: str = ..., probabilityCol: str = ..., rawPredictionCol: str = ..., maxDepth: int = ..., maxBins: int = ..., minInstancesPerNode: int = ..., minInfoGain: float = ..., maxMemoryInMB: int = ..., cacheNodeIds: bool = ..., checkpointInterval: int = ..., impurity: str = ..., seed: Optional[int] = ...) -> DecisionTreeClassifier: ...

class DecisionTreeClassificationModel(DecisionTreeModel, JavaClassificationModel, JavaMLWritable, JavaMLReadable):
    @property
    def featureImportances(self) -> Vector: ...

class RandomForestClassifier(JavaEstimator, HasFeaturesCol, HasLabelCol, HasPredictionCol, HasSeed, HasRawPredictionCol, HasProbabilityCol, RandomForestParams, TreeClassifierParams, HasCheckpointInterval, JavaMLWritable, JavaMLReadable):
    def __init__(self, featuresCol: str = ..., labelCol: str = ..., predictionCol: str = ..., probabilityCol: str = ..., rawPredictionCol: str = ..., maxDepth: int = ..., maxBins: int = ..., minInstancesPerNode: int = ..., minInfoGain: float = ..., maxMemoryInMB: int = ..., cacheNodeIds: bool = ..., checkpointInterval: int = ..., impurity: str = ..., numTrees: int = ..., featureSubsetStrategy: str = ..., seed: Optional[int] = ..., subsamplingRate: float = ...) -> None: ...
    def setParams(self, featuresCol: str = ..., labelCol: str = ..., predictionCol: str = ..., probabilityCol: str = ..., rawPredictionCol: str = ..., maxDepth: int = ..., maxBins: int = ..., minInstancesPerNode: int = ..., minInfoGain: float = ..., maxMemoryInMB: int = ..., cacheNodeIds: bool = ..., checkpointInterval: int = ..., seed: Optional[int] = ..., impurity: str = ..., numTrees: int = ..., featureSubsetStrategy: str = ..., subsamplingRate: float = ...) -> RandomForestClassifier: ...

class RandomForestClassificationModel(TreeEnsembleModel, JavaClassificationModel, JavaMLWritable, JavaMLReadable):
    @property
    def featureImportances(self) -> Vector: ...
    @property
    def trees(self) -> List[DecisionTreeClassificationModel]: ...

class GBTClassifier(JavaEstimator, HasFeaturesCol, HasLabelCol, HasPredictionCol, HasMaxIter, GBTParams, HasCheckpointInterval, HasStepSize, HasSeed, JavaMLWritable, JavaMLReadable):
    lossType = ...  # type: Param
    def __init__(self, featuresCol: str = ..., labelCol: str = ..., predictionCol: str = ..., maxDepth: int = ..., maxBins: int = ..., minInstancesPerNode: int = ..., minInfoGain: float = ..., maxMemoryInMB: int = ..., cacheNodeIds: bool = ..., checkpointInterval: int = ..., lossType: str = ..., maxIter: int = ..., stepSize: float = ..., seed: Optional[int] = ..., subsamplingRate: float = ...) -> None: ...
    def setParams(self, featuresCol: str = ..., labelCol: str = ..., predictionCol: str = ..., maxDepth: int = ..., maxBins: int = ..., minInstancesPerNode: int = ..., minInfoGain: float = ..., maxMemoryInMB: int = ..., cacheNodeIds: bool = ..., checkpointInterval: int = ..., lossType: str = ..., maxIter: int = ..., stepSize: float = ..., seed: Optional[int] = ..., subsamplingRate: float = ...) -> 'GBTClassifier': ...
    def setLossType(self, value: str) -> 'GBTClassifier': ...
    def getLossType(self) -> str: ...

class GBTClassificationModel(TreeEnsembleModel, JavaPredictionModel, JavaMLWritable, JavaMLReadable):
    @property
    def featureImportances(self) -> Vector: ...
    @property
    def trees(self) -> List[DecisionTreeRegressionModel]: ...

class NaiveBayes(JavaEstimator, HasFeaturesCol, HasLabelCol, HasPredictionCol, HasProbabilityCol, HasRawPredictionCol, HasThresholds, HasWeightCol, JavaMLWritable, JavaMLReadable):
    smoothing = ...  # type: Param
    modelType = ...  # type: Param
    def __init__(self, featuresCol: str = ..., labelCol: str = ..., predictionCol: str = ..., probabilityCol: str = ..., rawPredictionCol: str = ..., smoothing: float = ..., modelType: str = ..., thresholds: Optional[List[float]] = ..., weightCol: Optional[str] = ...) -> None: ...
    def setParams(self, featuresCol: str = ..., labelCol: str = ..., predictionCol: str = ..., probabilityCol: str = ..., rawPredictionCol: str = ..., smoothing: float = ..., modelType: str = ..., thresholds: Optional[List[float]] = ..., weightCol: Optional[str] = ...) -> 'NaiveBayes': ...
    def setSmoothing(self, value: float) -> 'NaiveBayes': ...
    def getSmoothing(self) -> float: ...
    def setModelType(self, value: str) -> 'NaiveBayes': ...
    def getModelType(self) -> str: ...

class NaiveBayesModel(JavaModel, JavaClassificationModel, JavaMLWritable, JavaMLReadable):
    @property
    def pi(self) -> Vector: ...
    @property
    def theta(self) -> Matrix: ...

class MultilayerPerceptronClassifier(JavaEstimator, HasFeaturesCol, HasLabelCol, HasPredictionCol, HasMaxIter, HasTol, HasSeed, HasStepSize, JavaMLWritable, JavaMLReadable):
    layers = ...  # type: Param
    blockSize = ...  # type: Param
    solver = ...  # type: Param
    initialWeights = ...  # type: Param
    def __init__(self, featuresCol: str = ..., labelCol: str = ..., predictionCol: str = ..., maxIter: int = ..., tol: float = ..., seed: Optional[int] = ..., layers: Optional[List[int]] = ..., blockSize: int = ..., stepSize: float = ..., solver: str = ..., initialWeights: Optional[Vector] = ...) -> None: ...
    def setParams(self, featuresCol: str = ..., labelCol: str = ..., predictionCol: str = ..., maxIter: int = ..., tol: float = ..., seed: Optional[int] = ..., layers: Optional[List[int]] = ..., blockSize: int = ..., stepSize: float = ..., solver: str = ..., initialWeights: Optional[Vector] = ...) -> 'MultilayerPerceptronClassifier': ...
    def setLayers(self, value: List[int]) -> 'MultilayerPerceptronClassifier': ...
    def getLayers(self) -> List[int]: ...
    def setBlockSize(self, value: int) -> 'MultilayerPerceptronClassifier': ...
    def getBlockSize(self) -> int: ...
    def setStepSize(self, value: float) -> 'MultilayerPerceptronClassifier': ...
    def getStepSize(self) -> float: ...
    def setSolver(self, value: str) -> 'MultilayerPerceptronClassifier': ...
    def getSolver(self) -> str: ...
    def setInitialWeights(self, value: Vector) -> 'MultilayerPerceptronClassifier': ...
    def getInitialWeights(self) -> Vector: ...

class MultilayerPerceptronClassificationModel(JavaModel, JavaPredictionModel, JavaMLWritable, JavaMLReadable):
    @property
    def layers(self) -> List[int]: ...
    @property
    def weights(self) -> Vector: ...

class OneVsRestParams(HasFeaturesCol, HasLabelCol, HasPredictionCol):
    classifier = ...  # type: Param
    def setClassifier(self, value: Estimator) -> 'OneVsRestParams': ...
    def getClassifier(self) -> Estimator: ...

class OneVsRest(Estimator, OneVsRestParams, MLReadable, MLWritable):
    def __init__(self, featuresCol: str = ..., labelCol: str = ..., predictionCol: str = ..., classifier: Optional[Estimator] = ...) -> None: ...
    def setParams(self, featuresCol: Optional[str] = ..., labelCol: Optional[str] = ..., predictionCol: Optional[str] = ..., classifier: Optional[Estimator] = ...) -> 'OneVsRest': ...
    def copy(self, extra: Optional[ParamMap] = ...) -> 'OneVsRest': ...
    def write(self) -> JavaMLWriter: ...
    def save(self, path: str) -> None: ...
    @classmethod
    def read(cls) -> JavaMLReader: ...

class OneVsRestModel(Model, OneVsRestParams, MLReadable, MLWritable):
    models = ...  # type: List[Transformer]
    def __init__(self, models: List[Transformer]) -> None: ...
    def copy(self, extra: Optional[ParamMap] = ...) -> 'OneVsRestModel': ...
    def write(self): ...
    def save(self, path: str) -> None: ...
    @classmethod
    def read(cls) -> JavaMLReader: ...
