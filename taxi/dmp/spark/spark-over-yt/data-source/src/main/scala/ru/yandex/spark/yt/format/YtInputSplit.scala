package ru.yandex.spark.yt.format

import org.apache.hadoop.fs.Path
import org.apache.hadoop.mapreduce.InputSplit
import org.apache.spark.sql.types.StructType
import ru.yandex.inside.yt.kosher.cypress.{RangeLimit, YPath}
import ru.yandex.inside.yt.kosher.impl.ytree.{YTreeDoubleNodeImpl, YTreeEntityNodeImpl, YTreeIntegerNodeImpl, YTreeStringNodeImpl}
import ru.yandex.inside.yt.kosher.ytree.YTreeNode
import ru.yandex.spark.yt.common.utils._
import ru.yandex.spark.yt.format.YtInputSplit._
import ru.yandex.spark.yt.format.conf.FilterPushdownConfig
import ru.yandex.spark.yt.fs.YPathEnriched.ypath
import ru.yandex.spark.yt.logger.{YtDynTableLogger, YtDynTableLoggerConfig, YtLogger}
import ru.yandex.spark.yt.serializers.PivotKeysConverter.toRangeLimit
import ru.yandex.spark.yt.serializers.SchemaConverter
import ru.yandex.spark.yt.serializers.SchemaConverter.MetadataFields

import scala.annotation.tailrec


case class YtInputSplit(file: YtPartitionedFile, schema: StructType,
                        pushedFilters: SegmentSet = SegmentSet(),
                        filterPushdownConfig: FilterPushdownConfig,
                        ytLoggerConfig: Option[YtDynTableLoggerConfig]) extends InputSplit {
  private implicit lazy val ytLog: YtLogger = YtDynTableLogger.pushdown(ytLoggerConfig)
  private val logMessageInfo = Map(
    "segments" -> pushedFilters.toString,
    "keysInFile" -> file.keyColumns.mkString(", "),
    "keysInSchema" -> SchemaConverter.keys(schema).mkString(", ")
  )

  override def getLength: Long = file.endRow - file.beginRow

  override def getLocations: Array[String] = Array.empty

  private val originalFieldNames = schema.fields.map(x => x.metadata.getString(MetadataFields.ORIGINAL_NAME))
  private val basePath: YPath = ypath(new Path(file.path)).toYPath.withColumns(originalFieldNames: _*)

  lazy val ytPath: YPath = calculateYtPath(pushing = false)
  lazy val ytPathWithFiltersDetailed: YPath = calculateYtPath(pushing = true, union = false)
  lazy val ytPathWithFilters: YPath = calculateYtPath(pushing = true, union = true)

  private def calculateYtPath(pushing: Boolean, union: Boolean = true): YPath = {
    if (file.isDynamic) {
      basePath.withRange(toRangeLimit(file.beginKey, file.keyColumns), toRangeLimit(file.endKey, file.keyColumns))
    } else {
      if (pushing && filterPushdownConfig.enabled) {
        val res = getYPath(union && filterPushdownConfig.unionEnabled)
        if (pushedFilters.map.nonEmpty) {
          ytLog.info("YtInputSplit pushed filters to ypath", logMessageInfo ++
            Map("union" -> union.toString, "ypath" -> res.toString))
        }
        res
      } else {
        basePath.withRange(file.beginRow, file.endRow)
      }
    }
  }

  private def getYPath(single: Boolean): YPath = {
    val tableKeys = SchemaConverter.keys(schema)
    val res = getYPathImpl(single, pushedFilters, tableKeys, filterPushdownConfig, basePath, file)
    if (tableKeys.length > 1 || tableKeys.contains(None)) {
      ytLog.warn("YtInputSplit pushed filters with more than one key column", logMessageInfo ++
        Map("union" -> single.toString, "ypath" -> res.toString))
    }
    res
  }
}

object YtInputSplit {
  private[format] def getYPathImpl(single: Boolean, pushedFilters: SegmentSet, keys: Seq[Option[String]],
                                   filterPushdownConfig: FilterPushdownConfig,
                                   basePath: YPath, file: YtPartitionedFile)
                                  (implicit ytLog: YtLogger = YtLogger.noop): YPath = {
    val rawYPathFilterSegments = getKeyFilterSegments(
      if (single) pushedFilters.simplifySegments else pushedFilters,
      keys.toList, filterPushdownConfig.ytPathCountLimit)(ytLog)

    ytLog.debug("YtInputSplit got key segments from filters", Map(
      "union" -> single.toString,
      "key segments" -> rawYPathFilterSegments.mkString(", "),
      "segments" -> pushedFilters.toString,
      "keysInFile" -> file.keyColumns.mkString(", "),
      "keysInSchema" -> keys.mkString(", ")
    ))

    if (rawYPathFilterSegments == List(Nil)) {
      basePath.withRange(file.beginRow, file.endRow)
    } else {
      rawYPathFilterSegments.foldLeft(basePath) {
        case (ypath, segment) =>
          ypath.withRange(
            getRangeLimit(prepareKeys(getLeftPoints(segment)), file.beginRow),
            getRangeLimit(prepareKeys(getRightPoints(segment)) :+ getMaximumKey, file.endRow)
          )
      }
    }
  }

  private[format] def getKeyFilterSegments(filterSegments: SegmentSet,
                                           keys: List[Option[String]],
                                           pathCountLimit: Int)
                                          (implicit ytLog: YtLogger = YtLogger.noop): List[List[Segment]] = {
    recursiveGetFilterSegmentsImpl(filterSegments, keys, pathCountLimit)(ytLog)
  }

  @tailrec
  private def recursiveGetFilterSegmentsImpl(filterSegments: SegmentSet,
                                             keys: List[Option[String]], pathCountLimit: Int,
                                             result: List[List[Segment]] = List(Nil))
                                            (implicit ytLog: YtLogger = YtLogger.noop): List[List[Segment]] = {
    keys match {
      case None :: tailKeys =>
        recursiveGetFilterSegmentsImpl(filterSegments, tailKeys, pathCountLimit,
          result.map(res => Segment.full +: res))
      case Some(headKey) :: tailKeys =>
        filterSegments.map.get(headKey) match {
          case None =>
            recursiveGetFilterSegmentsImpl(filterSegments, tailKeys, pathCountLimit,
              result.map(res => Segment.full +: res))
          case Some(segments) =>
            if (segments.size * result.size > pathCountLimit) {
              ytLog.debug(s"YtInputSplit got more than ${pathCountLimit} segments and stopped")
              result.map(_.reverse)
            } else {
              recursiveGetFilterSegmentsImpl(filterSegments, tailKeys, pathCountLimit,
                result.flatMap(res => segments.map(_ +: res)))
            }
        }
      case Nil =>
        result.map(_.reverse)
    }
  }

  private def getRangeLimit(keys: Seq[YTreeNode], rowIndex: Long = -1): RangeLimit = {
    import scala.collection.JavaConverters._
    new RangeLimit(keys.toList.asJava, rowIndex, -1)
  }

  private def getSpecifiedEntity(value: String): YTreeNode = {
    new YTreeEntityNodeImpl(java.util.Map.of("type", new YTreeStringNodeImpl(value, null)))
  }

  private def getMinimumKey: YTreeNode = getSpecifiedEntity("min")

  private def getMaximumKey: YTreeNode = getSpecifiedEntity("max")

  private def prepareKeys(array: Seq[Point]): Seq[YTreeNode] = {
    array.map {
      case MInfinity() => getMinimumKey
      case PInfinity() => getMaximumKey
      case rValue: RealValue[_] if rValue.value.isInstanceOf[Double] =>
        new YTreeDoubleNodeImpl(rValue.value.asInstanceOf[Double], null)
      case rValue: RealValue[_] if rValue.value.isInstanceOf[Long] =>
        new YTreeIntegerNodeImpl(true, rValue.value.asInstanceOf[Long], null)
      case rValue: RealValue[String] => new YTreeStringNodeImpl(rValue.value, null)
    }
  }

  private def getLeftPoints(array: Seq[Segment]): Seq[Point] = {
    array.map { case Segment(left, _) => left }
  }

  private def getRightPoints(array: Seq[Segment]): Seq[Point] = {
    array.map { case Segment(_, right) => right }
  }
}
