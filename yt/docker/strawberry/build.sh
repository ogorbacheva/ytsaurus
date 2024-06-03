#!/usr/bin/env bash

script_name=$0

image_tag=""
ytsaurus_source_path="."
output_path="."
image_cr=""

print_usage() {
    cat << EOF
Usage: $script_name [-h|--help]
                    [--ytsaurus-source-path /path/to/ytsaurus.repo (default: $ytsaurus_source_path)]
                    [--output-path /path/to/output (default: $output_path)]
                    [--image-tag some-tag (default: $image_tag)]
                    [--image-cr some-cr/ (default: $image_cr)]
EOF
    exit 1
}

# Parse options
while [[ $# -gt 0 ]]; do
    key="$1"
    case $key in
        --ytsaurus-source-path)
        ytsaurus_source_path="$2"
        shift 2
        ;;
        --output-path)
        output_path="$2"
        shift 2
        ;;
        --image-tag)
        image_tag="$2"
        shift 2
        ;;
        --image-cr)
        image_cr="$2"
        shift 2
        ;;
        -h|--help)
        print_usage
        shift
        ;;
        *)  # unknown option
        echo "Unknown argument $1"
        print_usage
        ;;
    esac
done

strawberry_controller="${ytsaurus_source_path}/yt/chyt/controller/cmd/chyt-controller/chyt-controller"
credits="${ytsaurus_source_path}/yt/docker/strawberry/credits"

dockerfile="${ytsaurus_source_path}/yt/docker/strawberry/Dockerfile"

cp ${strawberry_controller} ${output_path}
cp ${dockerfile} ${output_path}

mkdir ${output_path}/credits
cp -r ${credits}/chyt-controller.CREDITS ${output_path}/credits

cd ${output_path}

docker build -t ${image_cr}ytsaurus/strawberry:${image_tag} .
