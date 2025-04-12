#!/bin/bash

REPO_ROOT_PATH=$(git rev-parse --show-toplevel)
. ${REPO_ROOT_PATH}/tools/common.sh

FLAG_REBUILD_BASE_IMAGE='false'

DOCKERFILE_PATH_BASE="${REPO_ROOT_PATH}/ci_cd/docker_files/base_image_for_compiling"
DOCKERFILE_PATH_BASE_WITH_CACHE="${REPO_ROOT_PATH}/ci_cd/docker_files/base_image_for_compiling_with_cache"
BASE_IMAGE_NAME="shooting-star-base"
IMAGE_TAG_COMPILE_BASE="compile-base"
IMAGE_TAG_COMPILE_BASE_WITH_CACHE="compile-base-cached"
CONTAINER_NAME="shooting-star-img-building"

_start_docker_container() {
    image_name=$1
    image_tag=$2
    echo_info "Starting Docker container ${image_name}:${image_tag} with command..."

    container_run_cmd=("${DOCKER_CMD}" "run" "--name" "${CONTAINER_NAME}" "-v" "${REPO_ROOT_PATH}:/ShootingStar" "${image_name}:${image_tag}" "bash" "-c" "cd /ShootingStar && bazel build //src/weather_fetcher:all")
    echo "${container_run_cmd[@]}"
    "${container_run_cmd[@]}"
    if [[ $? != 0 ]]; then
        echo_error "Failed to start docker container with image ${IMAGE_NAME}:${IMAGE_TAG}."
        exit 4
    fi
    echo_info "Docker container started successfully."
}

_main_flow() {
    builtin cd ${REPO_ROOT_PATH}
    check_docker_is_available

    # Build base image.
    is_image_existing ${BASE_IMAGE_NAME} ${IMAGE_TAG_COMPILE_BASE}
    if [[ $? == 1 ]]; then
        echo_info "Docker image ${BASE_IMAGE_NAME}:${IMAGE_TAG_COMPILE_BASE} already exists."
        if [[ ${FLAG_REBUILD_BASE_IMAGE} == 'true' ]]; then
            echo
            stop_existing_docker_container_by_image ${BASE_IMAGE_NAME} ${IMAGE_TAG_COMPILE_BASE}
            echo
            remove_docker_image ${BASE_IMAGE_NAME} ${IMAGE_TAG_COMPILE_BASE}
            echo
            build_docker_image ${DOCKERFILE_PATH_BASE} ${BASE_IMAGE_NAME} ${IMAGE_TAG_COMPILE_BASE}
        else
            echo_info "Skip building Docker image ${BASE_IMAGE_NAME}:${IMAGE_TAG_COMPILE_BASE}."
        fi
    else
        build_docker_image ${DOCKERFILE_PATH_BASE} ${BASE_IMAGE_NAME} ${IMAGE_TAG_COMPILE_BASE}
    fi

    # Build base image with cache.
    echo
    stop_existing_docker_container_by_name ${CONTAINER_NAME}
    echo
    remove_container ${CONTAINER_NAME}
    is_image_existing ${BASE_IMAGE_NAME_WITH_CACHE} ${IMAGE_TAG_COMPILE_BASE_WITH_CACHE}
    if [[ $? == 1 ]]; then
        echo
        echo_info "Docker image ${BASE_IMAGE_NAME_WITH_CACHE}:${IMAGE_TAG_COMPILE_BASE_WITH_CACHE} already exists."
        remove_docker_image ${BASE_IMAGE_NAME_WITH_CACHE} ${IMAGE_TAG_COMPILE_BASE_WITH_CACHE}
    fi
    echo
    _start_docker_container ${BASE_IMAGE_NAME} ${IMAGE_TAG_COMPILE_BASE}
    echo
    commit_container ${CONTAINER_NAME} ${BASE_IMAGE_NAME} ${IMAGE_TAG_COMPILE_BASE_WITH_CACHE}
}

while [[ "$#" -gt 0 ]]; do
    case $1 in
        --docker-cmd)
            shift
            DOCKER_CMD=$1
            ;;
        --rebuild-base-image)
            FLAG_REBUILD_BASE_IMAGE='true'
            ;;
        *)
            ARGS="${ARGS} $1"
            ;;
    esac
    shift
done

_main_flow
