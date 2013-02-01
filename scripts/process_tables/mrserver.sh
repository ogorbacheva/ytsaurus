#!/bin/sh

./pull_table_from_mr.py --tables "//home/ignat/mrserver" --destination="//statbox" --server "mrserver1e.mr.yandex.net" --job-count 10 --proxy mrproxy1e.mr.yandex.net --proxy mrproxy2e.mr.yandex.net --proxy mrproxy3e.mr.yandex.net --pool "mrserver_restricted" --force

