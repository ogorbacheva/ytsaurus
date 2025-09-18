# Starting a Spark cluster

This section contains expanded instructions for starting a Spark cluster.  Basic start operations are described in the [Quick start](../../../../../user-guide/data-processing/spyt/launch.md#standalone) section.

{% note warning "Attention!" %}

A started Spark cluster statically occupies the resources allocated to it. So, it is recommended that the cluster be started in a separate computational pool with guaranteed resources. It makes sense to use one cluster for the command, and recycle the resources among several users. It is recommended to use [direct submitting](../../../../../user-guide/data-processing/spyt/launch.md#submit) when it is not planned to run Spark tasks at a high intensity (less than one time in an hour).


{% endnote %}

## Auto-scaler { #auto-scale }

To save resources of the computational pool if the load is low, a special auto-scaler mode can be turned on in Spark, which proportionally decreases the resources used.

### How it works { #how-works }

The operation YTsaurus has an `update_operation_parameters` method that enables the operation parameters to be changed.  The number of jobs in the operation can be changed via the `user_slots` parameter.  When the parameter is changed, the scheduler stops some of the jobs, or launches new ones (within the limit specified at the start of the operation).  Since the scheduler believes that all jobs in the operation are the same, this scaling method, performed in the regular mode in Spark, could lead to loss of the master or the history server, as well as of workers that Spark job drivers are being performed for.  To prevent disruptions to the Spark cluster's operation, it is started not as a single YTsaurus operation, but as several. This way, one operation is allocated to the dynamically changing set of workers, and can scale within the limits configured at the start.  In one or two other operations, a master and history-server (one operation), or a driver (when drivers are launched on the cluster, two operations) are performed.


### Starting a cluster with an auto-scaler { #start-auto }

Additional parameters are used for the `spark-launch-yt` launch script, or similar parameters of the SPYT client library.

- The `autoscaler-period <period>` is the frequency of auto-scaler launches, and (potentially) of changes in the operation settings. The period is programmed in `<length><unit of measurement [d|h|min|s|ms|µs|ns]>`.
- `enable-multi-operation-mode` turns on Spark start mode in multiple YTsaurus operations.
- `enable-dedicated-driver-operation-mode` launches workers for drivers in a separate YTsaurus operation.
- `driver-num <number of workers>` allocates a certain number of workers for the driver.
- `autoscaler-max-free-workers` is the maximum value of free workers (all superfluous workers will be stopped).
- `autoscaler-slot-increment-step` is the increment in which the number of workers is increased when the cluster is automatically expanded.

Example:

```bash
$ spark-launch-yt
--proxy <cluster_name>
--autoscaler-period 1s
--enable-multi-operation-mode
--discovery-path //discovery/path
```



## Updating a cluster { #update-cluster }

To update a Spark cluster, the following actions must be performed:

1. Stop the operation with the current cluster in YTsaurus. You can find a link to the operation using `spark-discovery-yt`.
2. Start a cluster using `spark-launch-yt`. The desired version can be specified in the `spark-cluster-version` argument.  If no version is specified, the last version will be started.


