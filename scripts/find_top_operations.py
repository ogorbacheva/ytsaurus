import argparse
import pprint
import sys

import yt.wrapper as yt


class Operation(object):
    def __init__(self, operation_id, cluster, batch_client):
        self._batch_client = batch_client
        self._cluster = cluster

        self.operation_id = operation_id
        self.errors = []
        self._attrs = None
        self._snapshot_size = None
        self._orchid = None
        self._output_resource_usage = None
        self._output_chunks = None
        self._output_disk_space = None
        self._input_locked_nodes = None
        self._input_disk_usage = None

    def fetch_attrs(self):
        attrs = self._batch_client.get("//sys/operations/{}/@".format(self.operation_id))
        yield

        if attrs.is_ok():
            self._attrs = attrs.get_result()
        else:
            self.errors.append(attrs.get_error())

    def fetch_snapshot_size(self):
        snapshot_size = self._batch_client.get("//sys/operations/{}/snapshot/@uncompressed_data_size".format(self.operation_id))
        yield

        if snapshot_size.is_ok():
            self._snapshot_size = snapshot_size.get_result()
        else:
            self.errors.append(snapshot_size.get_error())
    
    def fetch_output_resource_usage(self, in_account=None, debug_output=False):
        if self._attrs is None:
            yield
            return

        output_tx = self._attrs["output_transaction_id" if not debug_output else "async_scheduler_transaction_id"]
        output_resource_usage = self._batch_client.get("#{}/@resource_usage".format(output_tx))
        yield

        if output_resource_usage.is_ok():
            self._output_resource_usage = output_resource_usage.get_result()
        else:
            self.errors.append(output_resource_usage.get_error())
            return

        chunks = 0
        disk_space = 0
        for account, usage in self._output_resource_usage.items():
            if in_account is None or in_account == account:
                chunks += usage["chunk_count"]
                disk_space += usage["disk_space"]
        self._output_chunks = chunks
        self._output_disk_space = disk_space
            
    def fetch_input_locked_nodes(self):
        if self._attrs is None:
            yield
            return
        
        input_tx = self._attrs["input_transaction_id"]
        input_locked_nodes = self._batch_client.get("#{}/@locked_node_ids".format(input_tx))
        yield

        if input_locked_nodes.is_ok():
            self._input_locked_nodes = input_locked_nodes.get_result()
        else:
            self.errors.append(input_locked_nodes.get_error())

    def fetch_input_disk_usage(self, in_account=None):
        if self._input_locked_nodes is None:
            yield
            return
            
        input_object_attrs = []
        for object_id in self._input_locked_nodes:
            input_object_attrs.append(self._batch_client.get("#{}/@".format(object_id)))

        yield

        disk_space = 0
        for input_object in input_object_attrs:
            if not input_object.is_ok():
                self.errors.append(input_object.get_error())
                continue

            if in_account is not None and input_object.get_result()["account"] != in_account:
                continue

            disk_space += input_object.get_result()["resource_usage"]["disk_space"]
        self._input_disk_usage = disk_space

    def fetch_orchid(self):
        orchid = self._batch_client.get("//sys/scheduler/orchid/scheduler/operations/{}".format(self.operation_id))
        yield
        if orchid.is_ok():
            self._orchid = orchid.get_result()
        else:
            self.errors.append(orchid.get_error())        

    def get_default_attrs(self):
        if self._attrs is None:
            return None

        return {
            "operation_type": self._attrs["operation_type"],
            "authenticated_user": self._attrs["authenticated_user"],
            "pool": self._attrs["pool"],
            "title": self._attrs["spec"].get("title", "")
        }
            
    def get_job_count(self):
        if self._attrs is None:
            return None
        
        progress = self._attrs.get("progress")
        if progress is not None and "jobs" in progress:
            return progress["jobs"]["total"]
        else:
            return None

    def get_snapshot_size(self):
        return self._snapshot_size
            
    def get_output_chunks(self):
        return self._output_chunks

    def get_output_disk_space(self):
        return self._output_disk_space

    def get_input_disk_usage(self):
        return self._input_disk_usage

    def get_url(self):
        return "https://yt.yandex-team.ru/{}/#page=operation&mode=detail&id={}".format(self._cluster, self.operation_id)


def report_operations(operations, top_k, key_field, key_name):
    candidates = []
    for op in operations:
        candidate = op.get_default_attrs()
        if candidate is None:
            continue

        key = key_field(op)
        if key is None:
            continue

        candidate[key_name] = key
        candidate["url"] = op.get_url()
        candidates.append(candidate)

    displayed = sorted(candidates, key=lambda x: x[key_name], reverse=True)[:top_k]

    fields = [key_name, "operation_type", "authenticated_user", "pool", "url"]
    displayed = [dict((field, field) for field in fields)] + displayed
    sizes = [max(len(str(op[field])) for op in displayed) for field in fields]
    fmt = " ".join("{" + field + ":" + (">" if field == key_name else "")  + str(size) + "}" for field, size in zip(fields, sizes))

    for op in displayed:
        print fmt.format(**op)
        

def fetch_batch(batch_client, operations, fetch):
    generators = []
    for op in operations:
        generators.append(fetch(op))
        next(generators[-1])
    batch_client.commit_batch()
    for gen in generators:
        try:
            next(gen)
        except StopIteration:
            pass
        
if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Analyze running scheduler state.")
    parser.add_argument("-k", type=int, default=10, help="Number of operation to show (default: 10)")
    parser.add_argument("--proxy", help='Proxy alias')
    choices = ["job-count", "snapshot-size", "input-disk-usage", "output-chunks", "debug-output-chunks", "debug-output-disk-space"]
    parser.add_argument("--top-by", choices=choices, default="job-count")
    parser.add_argument("--in-account", help="Count resource usage only in this account")
    parser.add_argument("--show-errors", default=False, action="store_true")

    args = parser.parse_args()

    client = yt.YtClient(proxy=args.proxy)
    batch_client = client.create_batch_client()
    operations = [Operation(operation_id, args.proxy, batch_client) for operation_id in client.list("//sys/operations")]

    fetch_batch(batch_client, operations, lambda op: op.fetch_attrs())

    if args.top_by == "job-count":
        report_operations(operations, args.k, lambda op: op.get_job_count(), args.top_by)
    elif args.top_by == "snapshot-size":
        fetch_batch(batch_client, operations, lambda op: op.fetch_snapshot_size())

        report_operations(operations, args.k, lambda op: op.get_snapshot_size(), args.top_by)
    elif args.top_by == "input-disk-usage":
        fetch_batch(batch_client, operations, lambda op: op.fetch_input_locked_nodes())
        fetch_batch(batch_client, operations, lambda op: op.fetch_input_disk_usage(args.in_account))

        report_operations(operations, args.k, lambda op: op.get_input_disk_usage(), args.top_by)
    elif args.top_by == "output-chunks":
        fetch_batch(batch_client, operations, lambda op: op.fetch_output_resource_usage(args.in_account))

        report_operations(operations, args.k, lambda op: op.get_output_chunks(), args.top_by)

    elif args.top_by in ("debug-output-chunks", "debug-output-disk-space"):
        fetch_batch(batch_client, operations, lambda op: op.fetch_output_resource_usage(args.in_account, True))

        if args.top_by == "debug-output-chunks":
            report_operations(operations, args.k, lambda op: op.get_output_chunks(), args.top_by)
        else:
            report_operations(operations, args.k, lambda op: op.get_output_disk_space(), args.top_by)

    if args.show_errors:
        for op in operations:
            if not op.errors:
                continue
            pprint.pprint(op.errors, stream=sys.stderr)
    else:
        num_errors = sum(len(op.errors) for op in operations)
        if num_errors > 0:
            print >>sys.stderr, "WARNING: {} errors, rerun with --show-errors see full error messages".format(num_errors)
