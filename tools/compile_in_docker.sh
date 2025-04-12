#!/bin/bash

REPO_ROOT_PATH=$(git rev-parse --show-toplevel)
. ${REPO_ROOT_PATH}/tools/common.sh

FLAG_REBUILD_BASE_IMAGE='false'

BASE_IMAGE_NAME="shooting-star-base"
BASE_IMAGE_TAG="compile-base-cached"
CONTAINER_NAME="shooting-star-compiling"
TARGETS=''

_start_docker_container_to_compile() {
    image_name=${BASE_IMAGE_NAME}
    image_tag=${BASE_IMAGE_TAG}

    echo_info "Starting Docker container ${image_name}:${image_tag} with command..."
    container_run_cmd=("${DOCKER_CMD}" "run" "--name" "${CONTAINER_NAME}" "-v" "${REPO_ROOT_PATH}:/ShootingStar" "${image_name}:${image_tag}" "bash" "-c" "cd /ShootingStar && bazel build ${TARGETS} && cp -r bazel-bin/src binaries/")
    echo "${container_run_cmd[@]}"
    "${container_run_cmd[@]}"
    if [[ $? != 0 ]]; then
        echo_error "Failed to start docker container with image ${IMAGE_NAME}:${IMAGE_TAG}."
        exit 4
    fi
    echo_info "Docker container started and executed successfully."
}

_main_flow() {
    builtin cd ${REPO_ROOT_PATH}
    check_docker_is_available

    # Check base image.
    _is_image_existing ${BASE_IMAGE_NAME} ${BASE_IMAGE_TAG}
    if [[ $? == 0 ]]; then
        pull_docker_image ${BASE_IMAGE_NAME} ${BASE_IMAGE_TAG}

        if [[ $? == 0 ]]; then
            echo
            echo_error "Prepare base image of ${BASE_IMAGE_NAME}:${BASE_IMAGE_TAG} first."
            exit 1
        fi
    fi

    rm ./bazel*
    [[ ! -d ./binaries ]] && mkdir ./binaries
    echo
    stop_existing_docker_container_by_name ${CONTAINER_NAME}
    echo
    remove_container ${CONTAINER_NAME}
    echo
    _start_docker_container_to_compile
    echo
    remove_container ${CONTAINER_NAME}
}

while [[ "$#" -gt 0 ]]; do
    case $1 in
        --docker-cmd)
            shift
            DOCKER_CMD=$1
            ;;
        --base-img)
            shift
            BASE_IMAGE_NAME=$1
            ;;
        --base-img-tag)
            shift
            BASE_IMAGE_TAG=$1
            ;;
        *)
            TARGETS="${TARGETS} $1"
            ;;
    esac
    shift
done

_main_flow
