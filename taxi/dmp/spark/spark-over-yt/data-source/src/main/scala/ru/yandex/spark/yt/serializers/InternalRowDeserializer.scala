package ru.yandex.spark.yt.serializers

import java.io.ByteArrayOutputStream
import org.apache.spark.sql.catalyst.InternalRow
import org.apache.spark.sql.catalyst.expressions.GenericInternalRow
import org.apache.spark.sql.sources.Filter
import org.apache.spark.sql.types._
import org.apache.spark.sql.yson.{UInt64Type, YsonType}
import org.apache.spark.unsafe.types.{CalendarInterval, UTF8String}
import ru.yandex.inside.yt.kosher.common.Decimal.binaryToText
import ru.yandex.inside.yt.kosher.impl.ytree.builder.YTreeBuilder
import ru.yandex.inside.yt.kosher.impl.ytree.serialization.YTreeBinarySerializer
import ru.yandex.inside.yt.kosher.impl.ytree.serialization.spark.YsonDecoder
import ru.yandex.yt.ytclient.`object`.{WireRowDeserializer, WireValueDeserializer}
import ru.yandex.yt.ytclient.tables.ColumnValueType

import java.math.BigInteger
import java.sql.{Date, Timestamp}
import java.util.concurrent.TimeUnit
import scala.collection.mutable
import scala.concurrent.duration.Duration

class InternalRowDeserializer(schema: StructType) extends WireRowDeserializer[InternalRow] with WireValueDeserializer[Any] {
  private var _values: Array[Any] = _
  private val indexedSchema = schema.fields.map(_.dataType).toIndexedSeq
  private val indexedDataTypes = schema.fields.map(f => SchemaConverter.indexedDataType(f.dataType))

  private var _index = 0

  override def onNewRow(columnCount: Int): WireValueDeserializer[_] = {
    _values = new Array[Any](schema.length)
    _index = 0
    this
  }

  override def onCompleteRow(): InternalRow = {
    new GenericInternalRow(_values)
  }

  override def onNullRow(): InternalRow = {
    throw new IllegalArgumentException("Null rows are not supported")
  }

  override def setId(id: Int): Unit = {
    _index = id
  }

  override def setType(`type`: ColumnValueType): Unit = {}

  override def setAggregate(aggregate: Boolean): Unit = {}

  override def setTimestamp(timestamp: Long): Unit = {}

  override def build(): Any = null

  private def addValue(value: Any): Unit = {
    if (_index < _values.length) {
      _values(_index) = value
    }
  }

  override def onEntity(): Unit = addValue(null)

  override def onInteger(value: Long): Unit = {
    if (_index < _values.length) {
      indexedSchema(_index) match {
        case LongType => addValue(value)
        case IntegerType => addValue(value.toInt)
        case ShortType => addValue(value.toShort)
        case ByteType => addValue(value.toByte)
        case DateType => addValue(value.toInt)
        case TimestampType => addValue(value * 1000000)
        case YsonType => addValue(toYsonBytes(value))
        case UInt64Type => addValue(value)
      }
    }
  }

  override def onBoolean(value: Boolean): Unit = {
    if (_index < _values.length) {
      indexedSchema(_index) match {
        case BooleanType => addValue(value)
        case YsonType => addValue(toYsonBytes(value))
      }
    }
  }

  override def onDouble(value: Double): Unit = {
    if (_index < _values.length) {
      indexedSchema(_index) match {
        case FloatType => addValue(value.toFloat)
        case DoubleType => addValue(value)
        case YsonType => addValue(toYsonBytes(value))
      }
    }
  }

  override def onBytes(bytes: Array[Byte]): Unit = {
    indexedSchema(_index) match {
      case BinaryType => addValue(bytes)
      case YsonType => addValue(bytes)
      case StringType => addValue(UTF8String.fromBytes(bytes))
      case d: DecimalType =>
        addValue(Decimal(BigDecimal(binaryToText(bytes, d.precision, d.scale)), d.precision, d.scale))
      case _ @ (ArrayType(_, _) | StructType(_) | MapType(_, _, _)) =>
        addValue(YsonDecoder.decode(bytes, indexedDataTypes(_index)))
    }
  }

  def toYsonBytes(value: Any): Array[Byte] = {
    val output = new ByteArrayOutputStream(64)
    val node = new YTreeBuilder().value(value).build()
    YTreeBinarySerializer.serialize(node, output)
    output.toByteArray
  }
}

object InternalRowDeserializer {
  private val deserializers: ThreadLocal[mutable.Map[StructType, InternalRowDeserializer]] = ThreadLocal.withInitial(() => mutable.ListMap.empty)

  def getOrCreate(schema: StructType, filters: Array[Filter] = Array.empty): InternalRowDeserializer = {
    deserializers.get().getOrElseUpdate(schema, new InternalRowDeserializer(schema))
  }
}
