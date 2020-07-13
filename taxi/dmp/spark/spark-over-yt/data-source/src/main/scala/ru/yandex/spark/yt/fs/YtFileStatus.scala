package ru.yandex.spark.yt.fs

import org.apache.hadoop.fs.{FileStatus, LocatedFileStatus}

class YtFileStatus(val path: YtPath,
                   val avgChunkSize: Long)
  extends LocatedFileStatus(new FileStatus(path.rowCount, false, 1, path.rowCount, 0, path), Array.empty) {

  def optimizedForScan: Boolean = path match {
    case _: YtDynamicPath => false
    case _ => true // TODO
  }
}
