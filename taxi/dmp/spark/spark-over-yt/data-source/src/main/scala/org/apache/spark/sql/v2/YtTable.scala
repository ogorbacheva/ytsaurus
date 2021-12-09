package org.apache.spark.sql.v2

import org.apache.hadoop.fs.FileStatus
import org.apache.spark.sql.SparkSession
import org.apache.spark.sql.connector.write.{LogicalWriteInfo, WriteBuilder}
import org.apache.spark.sql.execution.datasources.FileFormat
import org.apache.spark.sql.execution.datasources.v2.FileTable
import org.apache.spark.sql.types._
import org.apache.spark.sql.util.CaseInsensitiveStringMap
import org.apache.spark.sql.yson.YsonType

import scala.annotation.tailrec
import scala.collection.JavaConverters._

case class YtTable(name: String,
                   sparkSession: SparkSession,
                   options: CaseInsensitiveStringMap,
                   paths: Seq[String],
                   userSpecifiedSchema: Option[StructType],
                   fallbackFileFormat: Class[_ <: FileFormat])
  extends FileTable(sparkSession, options, paths, userSpecifiedSchema) {

  override def newScanBuilder(options: CaseInsensitiveStringMap): YtScanBuilder =
    YtScanBuilder(sparkSession, fileIndex, schema, dataSchema, options)

  override def inferSchema(files: Seq[FileStatus]): Option[StructType] =
    YtUtils.inferSchema(sparkSession, options.asScala.toMap, files)

  override def newWriteBuilder(info: LogicalWriteInfo): WriteBuilder =
    new YtWriteBuilder(paths, formatName, supportsDataType, info)

  override def supportsDataType(dataType: DataType): Boolean = YtTable.supportsDataType(dataType)

  override def formatName: String = "YT"
}

object YtTable {
  @tailrec
  def supportsDataType(dataType: DataType): Boolean = dataType match {
    case _: NullType => true

    case _: AtomicType => true

    case st: StructType => st.forall { f => supportsInnerDataType(f.dataType) }

    case ArrayType(elementType, _) => supportsInnerDataType(elementType)

    case MapType(keyType, valueType, _) =>
      supportsInnerDataType(keyType) && supportsInnerDataType(valueType)

    case udt: UserDefinedType[_] => supportsDataType(udt.sqlType)

    case _ => false
  }

  private def supportsInnerDataType(dataType: DataType): Boolean = dataType match {
    case YsonType => false
    case _ => supportsDataType(dataType)
  }
}
