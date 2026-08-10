# Inter-cluster network bandwidth throttling

Some {{product-name}} operations read data from other clusters. For example, the [RemoteCopy]({{ docs_root }}/core/user-guide/reference/data-processing/operations/remote-copy) operation copies data from one cluster to another. MapReduce operations can also read input tables from other clusters by using the [cluster]({{ docs_root }}/core/user-guide/reference/storage/ypath#rich_ypath_prefix) attribute in YPath. Such operations are referred to as operations with **remote read**.

To control load on network channels between clusters, {{product-name}} provides an inter-cluster network bandwidth throttling mechanism based on **distributed throttlers** (cluster throttlers). For details about the mechanism and its configuration, see [Inter-cluster Network Bandwidth Throttling]({{ docs_root }}/core/admin-guide/how-to-guides/cluster-throttlers) in the administrator guide.

## What is throttled { #what-is-throttled }

The bandwidth between {{product-name}} clusters is throttled. Throttling applies to:

- All [RemoteCopy]({{ docs_root }}/core/user-guide/reference/data-processing/operations/remote-copy) operations.
- MapReduce operations that read input tables from remote clusters.

{{ core-user-guide-reference-data-processing-operations-cluster-throttlers-md-audience-1 }}

## Impact on user operations { #user-impact }

Inter-cluster bandwidth limits can slow down operations that read data from other clusters. Throttling happens at two levels:

1. **At the job level** — a job cannot read data faster than its allocated limit.
2. **At the controller agent level** — if inter-cluster bandwidth is unavailable, new operation jobs are not started until bandwidth becomes available.

### How to tell if an operation is hitting the network limit { #network-limit-alert }

If an operation cannot schedule jobs because inter-cluster bandwidth is unavailable, the operation is assigned the alert **Unavailable network bandwidth to clusters**.

You can view the alert:

* **In the web interface** on the operation page:
{{ core-user-guide-reference-data-processing-operations-cluster-throttlers-md-audience-2 }}
* **Using the CLI** with the `get-operation` command:

   ```bash
   yt get-operation <operation-id> --attribute alerts
   ```

   Example output when an operation is hitting the network limit:
   ```json
   {
     "alerts": [
       {
         "type": "unavailable_network_bandwidth_to_clusters",
         "message": "Not enough network bandwidth to clusters"
       }
     ]
   }
   ```

### How to check inter-cluster channel utilization { #channel-utilization }

To view utilization of the network channel from the `remote` cluster to the `local` cluster, run the following command on the `local` cluster:

```
yt --proxy local list \
  "//sys/discovery_servers/<discovery-server>/orchid/discovery_server/remote_cluster_throttlers_group/@members" \
  --attribute local_throttlers --format json | jq '[.[] | ."$attributes"."local_throttlers"."bandwidth_remote"."rate"] | add'
```

{{ core-user-guide-reference-data-processing-operations-cluster-throttlers-md-audience-3 }}

### Impact on scheduling { #scheduling-impact }

Inter-cluster bandwidth is not a scheduler resource in the fair-share sense. However, throttling still affects job scheduling: if inter-cluster bandwidth is unavailable, the operation controller signals to the scheduler that it does not need resources, so new jobs are not started until bandwidth becomes available. Jobs that are already running continue to execute with the inter-cluster bandwidth limit applied.

### Recommendations { #recommendations }

If you need to read a large volume of data from another cluster, on the order of tens of terabytes or more, limit the number of jobs that can run concurrently for the operation. This helps avoid excessive load on the inter-cluster network channel.

#### Choosing a `user_slots` value { #user-slots-value }

Assuming a read speed of about 50 MB/s per job, a good starting point is `user_slots=300-500`. This yields a transfer rate of about 15-25 GB/s, which is reasonable for most scenarios.

The specific value depends on:
- Available inter-cluster bandwidth
- Volume of data to be transferred
- Required operation completion time
- Load on the network channel from other operations

#### Ways to limit the number of jobs { #limit-jobs }

You can limit the number of jobs in two ways:

* **Set a `user_slots` limit on the operation** via the operation specification. The `user_slots` parameter limits the number of jobs per cluster node, and each job always consumes exactly one user slot. For example, you can specify `resource_limits={user_slots=200}` in the specification. In this case, no more than 200 jobs will run simultaneously for the operation in each tree. For more details on configuring operation resources, see [Configuring operation compute resources]({{ docs_root }}/core/user-guide/how-to-guides/data-processing/scheduler/pool-settings#operations).
* **Use a pool with a `user_slots` limit**. You can set a resource limit for a pool by using the `resource_limits` attribute, which includes `user_slots`. For example, you can set a limit for a pool with the command `yt set //sys/pools/.../<your_pool>/@resource_limits '{user_slots=2000}'`. For more details on pool configuration, see [Pool settings]({{ docs_root }}/core/user-guide/how-to-guides/data-processing/scheduler/pool-settings).

## Configuration { #configuration }

Inter-cluster bandwidth throttling is configured by the cluster administrator. For details, see [Inter-cluster Network Bandwidth Throttling]({{ docs_root }}/core/admin-guide/how-to-guides/cluster-throttlers) in the administrator guide.
