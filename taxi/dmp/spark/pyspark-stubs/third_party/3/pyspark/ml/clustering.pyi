# Stubs for pyspark.ml.clustering (Python 3.5)
#

from typing import Any, List, Optional
from pyspark.ml.linalg import Matrix, Vector
from pyspark.ml.util import *
from pyspark.ml.wrapper import JavaEstimator, JavaModel, JavaParams, JavaWrapper
from pyspark.ml.param.shared import *
from pyspark.sql.dataframe import DataFrame
from numpy import ndarray  # type: ignore

class ClusteringSummary(JavaWrapper):
    @property
    def predictionCol(self) -> str: ...
    @property
    def predictions(self) -> DataFrame: ...
    @property
    def featuresCol(self) -> str: ...
    @property
    def k(self) -> int: ...
    @property
    def cluster(self) -> DataFrame: ...
    @property
    def clusterSizes(self) -> List[int]: ...
    @property
    def numIter(self) -> int: ...

class GaussianMixtureModel(JavaModel, JavaMLWritable, JavaMLReadable, HasTrainingSummary[GaussianMixtureSummary]):
    @property
    def weights(self) -> List[float]: ...
    @property
    def gaussiansDF(self) -> DataFrame: ...
    @property
    def summary(self) -> GaussianMixtureSummary: ...

class GaussianMixture(JavaEstimator[GaussianMixtureModel], HasFeaturesCol, HasPredictionCol, HasMaxIter, HasTol, HasSeed, HasProbabilityCol, JavaMLWritable, JavaMLReadable):
    k: Param
    def __init__(self, featuresCol: str = ..., predictionCol: str = ..., k: int = ..., probabilityCol: str = ..., tol: float = ..., maxIter: int = ..., seed: Optional[int] = ...) -> None: ...
    def setParams(self, featuresCol: str = ..., predictionCol: str = ..., k: int = ..., probabilityCol: str = ..., tol: float = ..., maxIter: int = ..., seed: Optional[int] = ...) -> GaussianMixture: ...
    def setK(self, value: int) -> GaussianMixture: ...
    def getK(self) -> int: ...

class GaussianMixtureSummary(ClusteringSummary):
    @property
    def probabilityCol(self) -> str: ...
    @property
    def probability(self) -> DataFrame: ...
    @property
    def logLikelihood(self) -> float: ...

class KMeansSummary(ClusteringSummary):
    def trainingCost(self) -> float: ...

class KMeansModel(JavaModel, GeneralJavaMLWritable, JavaMLReadable, HasTrainingSummary[KMeansSummary]):
    def clusterCenters(self) -> List[ndarray]: ...
    @property
    def summary(self) -> KMeansSummary: ...

class KMeans(JavaEstimator[KMeansModel], HasDistanceMeasure, HasFeaturesCol, HasPredictionCol, HasMaxIter, HasTol, HasSeed, JavaMLWritable, JavaMLReadable):
    k: Param
    initMode: Param
    initSteps: Param
    def __init__(self, featuresCol: str = ..., predictionCol: str = ..., k: int = ..., initMode: str = ..., initSteps: int = ..., tol: float = ..., maxIter: int = ..., seed: Optional[int] = ..., distanceMeasure: str = ...) -> None: ...
    def setParams(self, featuresCol: str = ..., predictionCol: str = ..., k: int = ..., initMode: str = ..., initSteps: int = ..., tol: float = ..., maxIter: int = ..., seed: Optional[int] = ..., distanceMeasure: str = ...) -> KMeans: ...
    def setK(self, value: int) -> KMeans: ...
    def getK(self) -> int: ...
    def setInitMode(self, value: str) -> KMeans: ...
    def getInitMode(self) -> str: ...
    def setInitSteps(self, value: int) -> KMeans: ...
    def getInitSteps(self) -> int: ...
    def setDistanceMeasure(self, value: str) -> KMeans: ...
    def getDistanceMeasure(self) -> str: ...

class BisectingKMeansModel(JavaModel, JavaMLWritable, JavaMLReadable, HasTrainingSummary[BisectingKMeansSummary]):
    def clusterCenters(self) -> List[ndarray]: ...
    def computeCost(self, dataset: DataFrame) -> float: ...
    @property
    def summary(self) -> BisectingKMeansSummary: ...

class BisectingKMeans(JavaEstimator[BisectingKMeansModel], HasDistanceMeasure, HasFeaturesCol, HasPredictionCol, HasMaxIter, HasSeed, JavaMLWritable, JavaMLReadable):
    k: Param
    minDivisibleClusterSize: Param
    def __init__(self, featuresCol: str = ..., predictionCol: str = ..., maxIter: int = ..., seed: Optional[int] = ..., k: int = ..., minDivisibleClusterSize: float = ..., distanceMeasure: str = ...) -> None: ...
    def setParams(self, featuresCol: str = ..., predictionCol: str = ..., maxIter: int = ..., seed: Optional[int] = ..., k: int = ..., minDivisibleClusterSize: float = ..., distanceMeasure: str = ...) -> BisectingKMeans: ...
    def setK(self, value: int) -> BisectingKMeans: ...
    def getK(self) -> int: ...
    def setMinDivisibleClusterSize(self, value: float) -> BisectingKMeans: ...
    def getMinDivisibleClusterSize(self) -> float: ...
    def setDistanceMeasure(self, value: str) -> BisectingKMeans: ...
    def getDistanceMeasure(self) -> str: ...

class BisectingKMeansSummary(ClusteringSummary):
    @property
    def trainingCost(self) -> float: ...

class LDAModel(JavaModel):
    def isDistributed(self) -> bool: ...
    def vocabSize(self) -> int: ...
    def topicsMatrix(self) -> Matrix: ...
    def logLikelihood(self, dataset: DataFrame) -> float: ...
    def logPerplexity(self, dataset: DataFrame) -> float: ...
    def describeTopics(self, maxTermsPerTopic: int = ...) -> DataFrame: ...
    def estimatedDocConcentration(self) -> Vector: ...

class DistributedLDAModel(LDAModel, JavaMLReadable, JavaMLWritable):
    def toLocal(self) -> LDAModel: ...
    def trainingLogLikelihood(self) -> float: ...
    def logPrior(self) -> float: ...
    def getCheckpointFiles(self) -> List[str]: ...

class LocalLDAModel(LDAModel, JavaMLReadable, JavaMLWritable): ...

class LDA(JavaEstimator[LDAModel], HasFeaturesCol, HasMaxIter, HasSeed, HasCheckpointInterval, JavaMLReadable, JavaMLWritable):
    k: Param
    optimizer: Param
    learningOffset: Param
    learningDecay: Param
    subsamplingRate: Param
    optimizeDocConcentration: Param
    docConcentration: Param
    topicConcentration: Param
    topicDistributionCol: Param
    keepLastCheckpoint: Param
    def __init__(self, featuresCol: str = ..., maxIter: int = ..., seed: Optional[int] = ..., checkpointInterval: int = ..., k: int = ..., optimizer: str = ..., learningOffset: float = ..., learningDecay: float = ..., subsamplingRate: float = ..., optimizeDocConcentration: bool = ..., docConcentration: Optional[List[float]] = ..., topicConcentration: Optional[float] = ..., topicDistributionCol: str = ..., keepLastCheckpoint: bool = ...) -> None: ...
    def setParams(self, featuresCol: str = ..., maxIter: int = ..., seed: Optional[int] = ..., checkpointInterval: int = ..., k: int = ..., optimizer: str = ..., learningOffset: float = ..., learningDecay: float = ..., subsamplingRate: float = ..., optimizeDocConcentration: bool = ..., docConcentration: Optional[List[float]] = ..., topicConcentration: Optional[float] = ..., topicDistributionCol: str = ..., keepLastCheckpoint: bool = ...) -> LDA: ...
    def setK(self, value: int) -> LDA: ...
    def getK(self) -> int: ...
    def setOptimizer(self, value: str) -> LDA: ...
    def getOptimizer(self) -> str: ...
    def setLearningOffset(self, value: float) -> LDA: ...
    def getLearningOffset(self) -> float: ...
    def setLearningDecay(self, value: float) -> LDA: ...
    def getLearningDecay(self) -> float: ...
    def setSubsamplingRate(self, value: float) -> LDA: ...
    def getSubsamplingRate(self) -> float: ...
    def setOptimizeDocConcentration(self, value: bool) -> LDA: ...
    def getOptimizeDocConcentration(self) -> bool: ...
    def setDocConcentration(self, value: List[float]) -> LDA: ...
    def getDocConcentration(self) -> List[float]: ...
    def setTopicConcentration(self, value: float) -> LDA: ...
    def getTopicConcentration(self) -> float: ...
    def setTopicDistributionCol(self, value: str) -> LDA: ...
    def getTopicDistributionCol(self) -> str: ...
    def setKeepLastCheckpoint(self, value: bool) -> LDA: ...
    def getKeepLastCheckpoint(self) -> bool: ...

class PowerIterationClustering(HasMaxIter, HasWeightCol, JavaParams, JavaMLReadable, JavaMLWritable):
    k: Param
    initMode: Param
    srcCol: Param
    dstCol: Param
    def __init__(self, k: int = ..., maxIter: int = ..., initMode: str = ..., srcCol: str = ..., dstCol: str = ..., weightCol: Optional[str] = ...) -> None: ...
    def setParams(self, k: int = ..., maxIter: int = ..., initMode: str = ..., srcCol: str = ..., dstCol: str = ..., weightCol: Optional[str] = ...) -> PowerIterationClustering: ...
    def setK(self, value: int) -> PowerIterationClustering: ...
    def getK(self) -> int: ...
    def setInitMode(self, value: str) -> PowerIterationClustering: ...
    def getInitMode(self) -> str: ...
    def setSrcCol(self, value: str) -> str: ...
    def getSrcCol(self) -> str: ...
    def setDstCol(self, value: str) -> PowerIterationClustering: ...
    def getDstCol(self) -> str: ...
    def assignClusters(self, dataset: DataFrame) -> DataFrame: ...
