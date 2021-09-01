package ru.yandex.spark.yt.fs

import org.apache.hadoop.fs._
import org.apache.hadoop.fs.permission.FsPermission
import org.apache.hadoop.util.Progressable
import org.apache.spark.sql.SparkSession
import org.slf4j.LoggerFactory
import ru.yandex.inside.yt.kosher.ytree.YTreeNode
import ru.yandex.spark.yt.fs.PathUtils.hadoopPathToYt
import ru.yandex.spark.yt.wrapper.YtWrapper
import ru.yandex.spark.yt.wrapper.cypress.PathType
import ru.yandex.spark.yt.wrapper.table.TableType
import ru.yandex.yt.ytclient.proxy.CompoundClient

import java.io.FileNotFoundException
import scala.language.postfixOps

@SerialVersionUID(1L)
class YtTableFileSystem extends YtFileSystemBase {
  private val log = LoggerFactory.getLogger(getClass)

  override def listStatus(f: Path): Array[FileStatus] = {
    log.debugLazy(s"List status $f")
    implicit val ytClient: CompoundClient = yt
    val path = hadoopPathToYt(f)

    val transaction = GlobalTableSettings.getTransaction(path)
    val attributes = YtWrapper.attributes(path, transaction)

    PathType.fromAttributes(attributes) match {
      case PathType.File => Array(new FileStatus(
        YtWrapper.fileSize(attributes), false, 1, 0, YtWrapper.modificationTimeTs(path), f
      ))
      case PathType.Table =>
        YtWrapper.tableType(attributes) match {
          case TableType.Static => listStaticTableAsFiles(f, attributes)
          case TableType.Dynamic =>
            if (!isDriver) throw new IllegalStateException("Listing dynamic tables on executors is not supported")
            listDynamicTableAsFiles(f, path, attributes)
        }
      case PathType.Directory => listYtDirectory(f, path, transaction)
      case pathType => throw new IllegalArgumentException(s"Can't list $pathType")
    }
  }

  private lazy val isDriver: Boolean = {
    SparkSession.getDefaultSession.nonEmpty
  }

  private def listStaticTableAsFiles(f: Path, attributes: Map[String, YTreeNode])
                                    (implicit yt: CompoundClient): Array[FileStatus] = {
    val rowCount = YtWrapper.rowCount(attributes)
    val optimizeMode = YtWrapper.optimizeMode(attributes)
    val chunkCount = YtWrapper.chunkCount(attributes)
    val tableSize = YtWrapper.dataWeight(attributes)
    val approximateRowSize = if (rowCount > 0) tableSize / rowCount else 0

    val filesCount = if (chunkCount > 0) chunkCount else 1
    val result = new Array[FileStatus](filesCount)
    for (chunkIndex <- 0 until chunkCount) {
      val chunkStart = chunkIndex * rowCount / chunkCount
      val chunkRowCount = (chunkIndex + 1) * rowCount / chunkCount - chunkStart
      val chunkPath = YtStaticPath(f, optimizeMode, chunkStart, chunkRowCount)
      result(chunkIndex) = new YtFileStatus(chunkPath, approximateRowSize)
    }

    if (chunkCount == 0) {
      // add path for schema resolving
      val chunkPath = YtStaticPath(f, optimizeMode, 0, 0)
      result(0) = new YtFileStatus(chunkPath, approximateRowSize)
    }
    result
  }

  private def listDynamicTableAsFiles(f: Path,
                                      path: String,
                                      attributes: Map[String, YTreeNode])
                                     (implicit yt: CompoundClient): Array[FileStatus] = {
    val pivotKeys = YtWrapper.pivotKeys(path) :+ YtWrapper.emptyPivotKey
    val keyColumns = YtWrapper.keyColumns(attributes)
    val result = new Array[FileStatus](pivotKeys.length - 1)
    val tableSize = YtWrapper.dataWeight(attributes)
    val approximateChunkSize = if (result.length > 0) tableSize / result.length else 0

    pivotKeys.sliding(2).zipWithIndex.foreach {
      case (Seq(startKey, endKey), i) =>
        val chunkPath = YtDynamicPath(f, startKey, endKey, i.toString, keyColumns)
        result(i) = new YtFileStatus(chunkPath, approximateChunkSize)
    }
    result
  }

  override def getFileStatus(f: Path): FileStatus = {
    log.debugLazy(s"Get file status $f")
    implicit val ytClient: CompoundClient = yt
    val path = hadoopPathToYt(f)
    val transaction = GlobalTableSettings.getTransaction(path)

    f match {
      case yp: YtPath =>
        new FileStatus(yp.rowCount, false, 1, yp.rowCount, 0, yp)
      case _ =>
        if (!YtWrapper.exists(path, transaction)) {
          throw new FileNotFoundException(s"File $path is not found")
        } else {
          val pathType = YtWrapper.pathType(path, transaction)
          pathType match {
            case PathType.Table => new FileStatus(0, true, 1, 0, 0, f)
            case PathType.File => new FileStatus(YtWrapper.fileSize(path, transaction), false, 1, 0, 0, f)
            case PathType.Directory => new FileStatus(0, true, 1, 0, 0, f)
            case PathType.None => null
          }
        }
    }
  }

  override def create(f: Path, permission: FsPermission, overwrite: Boolean, bufferSize: Int,
                      replication: Short, blockSize: Long, progress: Progressable): FSDataOutputStream = {
    create(f, permission, overwrite, bufferSize, replication, blockSize, progress, statistics)
  }
}
