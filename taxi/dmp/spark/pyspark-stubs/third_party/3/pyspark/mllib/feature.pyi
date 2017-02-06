# Stubs for pyspark.mllib.feature (Python 3.5)
#
# NOTE: This dynamically typed stub was automatically generated by stubgen.

from typing import Any
from pyspark.mllib.common import JavaModelWrapper
from pyspark.mllib.util import JavaLoader, JavaSaveable

class VectorTransformer:
    def transform(self, vector): ...

class Normalizer(VectorTransformer):
    p = ...  # type: Any
    def __init__(self, p: float = ...) -> None: ...
    def transform(self, vector): ...

class JavaVectorTransformer(JavaModelWrapper, VectorTransformer):
    def transform(self, vector): ...

class StandardScalerModel(JavaVectorTransformer):
    def transform(self, vector): ...
    def setWithMean(self, withMean): ...
    def setWithStd(self, withStd): ...
    @property
    def withStd(self): ...
    @property
    def withMean(self): ...
    @property
    def std(self): ...
    @property
    def mean(self): ...

class StandardScaler:
    withMean = ...  # type: Any
    withStd = ...  # type: Any
    def __init__(self, withMean: bool = ..., withStd: bool = ...) -> None: ...
    def fit(self, dataset): ...

class ChiSqSelectorModel(JavaVectorTransformer):
    def transform(self, vector): ...

class ChiSqSelector:
    numTopFeatures = ...  # type: Any
    selectorType = ...  # type: Any
    percentile = ...  # type: Any
    fpr = ...  # type: Any
    def __init__(self, numTopFeatures: int = ..., selectorType: str = ..., percentile: float = ..., fpr: float = ...) -> None: ...
    def setNumTopFeatures(self, numTopFeatures): ...
    def setPercentile(self, percentile): ...
    def setFpr(self, fpr): ...
    def setSelectorType(self, selectorType): ...
    def fit(self, data): ...

class PCAModel(JavaVectorTransformer): ...

class PCA:
    k = ...  # type: Any
    def __init__(self, k) -> None: ...
    def fit(self, data): ...

class HashingTF:
    numFeatures = ...  # type: Any
    binary = ...  # type: bool
    def __init__(self, numFeatures: Any = ...) -> None: ...
    def setBinary(self, value): ...
    def indexOf(self, term): ...
    def transform(self, document): ...

class IDFModel(JavaVectorTransformer):
    def transform(self, x): ...
    def idf(self): ...

class IDF:
    minDocFreq = ...  # type: Any
    def __init__(self, minDocFreq: int = ...) -> None: ...
    def fit(self, dataset): ...

class Word2VecModel(JavaVectorTransformer, JavaSaveable, JavaLoader):
    def transform(self, word): ...
    def findSynonyms(self, word, num): ...
    def getVectors(self): ...
    @classmethod
    def load(cls, sc, path): ...

class Word2Vec:
    vectorSize = ...  # type: int
    learningRate = ...  # type: float
    numPartitions = ...  # type: int
    numIterations = ...  # type: int
    seed = ...  # type: Any
    minCount = ...  # type: int
    windowSize = ...  # type: int
    def __init__(self) -> None: ...
    def setVectorSize(self, vectorSize): ...
    def setLearningRate(self, learningRate): ...
    def setNumPartitions(self, numPartitions): ...
    def setNumIterations(self, numIterations): ...
    def setSeed(self, seed): ...
    def setMinCount(self, minCount): ...
    def setWindowSize(self, windowSize): ...
    def fit(self, data): ...

class ElementwiseProduct(VectorTransformer):
    scalingVector = ...  # type: Any
    def __init__(self, scalingVector) -> None: ...
    def transform(self, vector): ...
