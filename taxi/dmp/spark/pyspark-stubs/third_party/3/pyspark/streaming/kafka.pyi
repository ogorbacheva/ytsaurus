# Stubs for pyspark.streaming.kafka (Python 3.5)
#

from typing import Any, Dict, Callable, List, Optional, TypeVar
from pyspark.rdd import RDD
from pyspark.context import SparkContext
from pyspark.streaming.context import StreamingContext, StorageLevel
from pyspark.streaming.dstream import DStream, TransformedDStream

T = TypeVar('T')
U = TypeVar('U')

def utf8_decoder(s: Optional[bytes]) -> Optional[str]: ...

class KafkaUtils:
    @staticmethod
    def createStream(ssc: StreamingContext, zkQuorum: str, groupId: str, topics: Dict[str, int], kafkaParams: Optional[Dict[str, str]] = ..., storageLevel: StorageLevel = ..., keyDecoder: Callable = ..., valueDecoder: Callable = ...) -> DStream: ...
    @staticmethod
    def createDirectStream(ssc: StreamingContext, topics, kafkaParams, fromOffsets: Optional[Any] = ..., keyDecoder: Any = ..., valueDecoder: Any = ..., messageHandler: Optional[Any] = ...) -> DStream: ...
    @staticmethod
    def createRDD(sc: SparkContext, kafkaParams: Dict[str, str], offsetRanges: List[int], leaders: Optional[Dict[TopicAndPartition, Broker]] = ..., keyDecoder: Any = ..., valueDecoder: Any = ..., messageHandler: Optional[Any] = ...) -> RDD: ...

class OffsetRange:
    topic = ...  # type: str
    partition = ...  # type: int
    fromOffset = ...  # type: int
    untilOffset = ...  # type: int
    def __init__(self, topic: str, partition: int, fromOffset: int, untilOffset: int) -> None: ...
    def __eq__(self, other: Any) -> bool: ...
    def __ne__(self, other: Any) -> bool: ...

class TopicAndPartition:
    def __init__(self, topic, partition) -> None: ...
    def __eq__(self, other): ...
    def __ne__(self, other): ...
    def __hash__(self): ...

class Broker:
    def __init__(self, host, port) -> None: ...

class KafkaRDD(RDD):
    def __init__(self, jrdd, ctx, jrdd_deserializer) -> None: ...
    def offsetRanges(self): ...

class KafkaDStream(DStream):
    def __init__(self, jdstream, ssc, jrdd_deserializer) -> None: ...
    def foreachRDD(self, func): ...
    def transform(self, func): ...

class KafkaTransformedDStream(TransformedDStream):
    def __init__(self, prev, func) -> None: ...

class KafkaMessageAndMetadata:
    topic = ...  # type: Any
    partition = ...  # type: Any
    offset = ...  # type: Any
    def __init__(self, topic, partition, offset, key, message) -> None: ...
    def __reduce__(self): ...
    @property
    def key(self): ...
    @property
    def message(self): ...
