package ru.yandex.spark.yt.common.utils

import org.apache.spark.sql.types.{DataType, StructField, StructType}

object TypeUtils {
  def tuple(elementTypes: Seq[DataType]): StructType = {
    StructType(
      elementTypes.zipWithIndex.map {case (element, i) => StructField(s"_${i + 1}", element)}
    )
  }

  def isTuple(struct: StructType): Boolean = {
    val structFieldNames = struct.fieldNames.toSeq
    val expectedTupleFieldNames = (1 to struct.length).map(n => s"_$n")
    structFieldNames == expectedTupleFieldNames
  }
}
