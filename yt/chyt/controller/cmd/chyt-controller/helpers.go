package main

import (
	"os"

	"a.yandex-team.ru/yt/go/ypath"
	"a.yandex-team.ru/yt/go/yson"
	"a.yandex-team.ru/yt/go/yterrors"
)

const (
	DefaultStrawberryRoot = ypath.Path("//sys/clickhouse/strawberry")
)

func readConfig(configPath string) yson.RawValue {
	content, err := os.ReadFile(configPath)
	if err != nil {
		panic(yterrors.Err("error reading config file", err))
	}
	return content
}

func loadConfig(configPath string, configStructure any) {
	content := readConfig(configPath)
	if err := yson.Unmarshal(content, configStructure); err != nil {
		panic(yterrors.Err("error parsing yson config", err))
	}
}

func getStrawberryRoot(root ypath.Path) ypath.Path {
	if root == "" {
		return DefaultStrawberryRoot
	}
	return root
}
