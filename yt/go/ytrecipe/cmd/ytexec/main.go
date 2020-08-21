package main

import (
	"a.yandex-team.ru/yt/go/mapreduce"
	"a.yandex-team.ru/yt/go/ytrecipe/internal/ytexec"
	"os"
)

func main() {
	if mapreduce.InsideJob() {
		os.Exit(mapreduce.JobMain())
	}
	ytexec.Main()
}
