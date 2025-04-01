#!/bin/bash

REPO_ROOT_PATH=$(git rev-parse --show-toplevel)
. ${REPO_ROOT_PATH}/tools/common.sh

FLAG_REBUILD_BASE_IMAGE='false'
DOCKER_CMD='docker'

DOCKERFILE_PATH_BASE="${REPO_ROOT_PATH}/ci_cd/docker_files/base_image_for_compiling"
DOCKERFILE_PATH_BASE_WITH_CACHE="${REPO_ROOT_PATH}/ci_cd/docker_files/base_image_for_compiling_with_cache"
BASE_IMAGE_NAME="shooting-star-base"
IMAGE_TAG_COMPILE_BASE="compile-base"
IMAGE_TAG_COMPILE_BASE_WITH_CACHE="compile-base-cached"
CONTAINER_NAME="shooting-star-img-building"

_build_docker_image() {
    dockerfile_path=$1
    image_name=$2
    image_tag=$3
    echo_info "Build docker image ${image_name}:${image_tag}..."
    echo_warning "${DOCKER_CMD} build -t ${image_name}:${image_tag} -f ${dockerfile_path} ${REPO_ROOT_PATH}"

    ${DOCKER_CMD} build -t ${image_name}:${image_tag} -f ${dockerfile_path} ${REPO_ROOT_PATH}

    if [[ $? != 0 ]]; then
        echo_error "Failed to build Docker image ${image_name}:${image_tag}."
        exit 3
    fi

    echo_info "docker image ${image_name}:${image_tag} built successfully."
}

_remove_docker_image() {
    image_name=$1
    image_tag=$2
    echo_info "Removing existing Docker image ${image_name}:${image_tag}..."
    echo_warning "${DOCKER_CMD} rmi ${image_name}:${image_tag}"

    ${DOCKER_CMD} rmi ${image_name}:${image_tag}

    if [[ $? != 0 ]]; then
        echo_error "Failed to remove existing Docker image ${image_name}:${image_tag}."
        exit 4
    fi

    echo_info "Docker image ${image_name}:${image_tag} removed successfully."
}

_is_image_existing() {
    image_name=$1
    image_tag=$2

    result=$(${DOCKER_CMD} images --all | sed 's/[[:space:]]\+/ /g' | grep -- "${image_name} ${image_tag}" | wc -l)
    if [[ $result == 0 ]]; then
        return 0
    fi

    return 1
}

_stop_existing_docker_container_by_image() {
    image_name=$1
    image_tag=$2
    result=$(${DOCKER_CMD} container list --all | grep -- "${image_name}:${image_tag}" | wc -l)
    if [[ $result == 0 ]]; then
        return
    fi

    try_count=5
    while [[ $try_count > 0 ]]; do
        echo_info "Stopping existing Docker container with image ${image_name}:${image_tag}..."

        container_id=$(${DOCKER_CMD} container list --all | grep -- "${image_name}:${image_tag}" | head -n 1 | sed 's/[[:space:]]\+/ /g' | cut -d ' ' -f 1)

        echo_warning "${DOCKER_CMD} container stop ${container_id}"
        ${DOCKER_CMD} container stop ${container_id}

        if [[ $? == 0 ]]; then
            break
        fi

        try_count=$((try_count - 1))
        sleep 3s

        result=$(${DOCKER_CMD} container list --all | grep -- "${image_name}:${image_tag}" | wc -l)
        if [[ $result == 0 ]]; then
            return
        fi

    done

    echo_info "Docker container stopped successfully."
}

_stop_existing_docker_container_by_name() {
    container_name=$1
    result=$(${DOCKER_CMD} container list --all | grep -- "${container_name}" | wc -l)
    if [[ $result == 0 ]]; then
        return
    fi

    try_count=3
    while [[ $try_count > 0 ]]; do
        echo_info "Stopping existing Docker container ${container_name}..."
        echo_warning "${DOCKER_CMD} container stop ${container_name}"

        ${DOCKER_CMD} container stop ${container_name}

        if [[ $? == 0 ]]; then
            break
        fi

        try_count=$((try_count - 1))
        sleep 3s

        result=$(${DOCKER_CMD} container list --all | grep -- "${container_name}" | wc -l)
        if [[ $result == 0 ]]; then
            return
        fi

    done

    echo_info "Docker container stopped successfully."
}

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

_remove_container() {
    container_name=$1
    result=$(${DOCKER_CMD} container list --all | grep -- "${container_name}" | wc -l)
    if [[ $result == 0 ]]; then
        return
    fi
    echo_info "Removing existing Docker container ${container_name}..."
    echo_warning "${DOCKER_CMD} container rm ${container_name}"

    ${DOCKER_CMD} container rm ${container_name}

    if [[ $? != 0 ]]; then
        echo_error "Failed to remove existing Docker container ${container_name}."
        exit 5
    fi

    echo_info "Docker container ${container_name} removed successfully."
}

_commit_container() {
    container_name=$1
    image_name=$2
    image_tag=$3
    echo_info "Committing Docker container ${container_name} to image ${image_name}:${image_tag}..."
    echo_warning "${DOCKER_CMD} commit ${container_name} ${image_name}:${image_tag}"

    ${DOCKER_CMD} commit ${container_name} ${image_name}:${image_tag}

    if [[ $? != 0 ]]; then
        echo_error "Failed to commit Docker container ${container_name} to image ${image_name}:${image_tag}."
        exit 6
    fi

    echo_info "Docker container ${container_name} committed to image ${image_name}:${image_tag} successfully."
}

_main_flow() {
    builtin cd ${REPO_ROOT_PATH}
    _check_docker_is_available

    # Build base image.
    _is_image_existing ${BASE_IMAGE_NAME} ${IMAGE_TAG_COMPILE_BASE}
    if [[ $? == 1 ]]; then
        echo_info "Docker image ${BASE_IMAGE_NAME}:${IMAGE_TAG_COMPILE_BASE} already exists."
        if [[ ${FLAG_REBUILD_BASE_IMAGE} == 'true' ]]; then
            _stop_existing_docker_container_by_image ${BASE_IMAGE_NAME} ${IMAGE_TAG_COMPILE_BASE}
            _remove_docker_image ${BASE_IMAGE_NAME} ${IMAGE_TAG_COMPILE_BASE}
            _build_docker_image ${DOCKERFILE_PATH_BASE} ${BASE_IMAGE_NAME} ${IMAGE_TAG_COMPILE_BASE}
        else
            echo_info "Skip building Docker image ${BASE_IMAGE_NAME}:${IMAGE_TAG_COMPILE_BASE}."
        fi
    else
        _build_docker_image ${DOCKERFILE_PATH_BASE} ${BASE_IMAGE_NAME} ${IMAGE_TAG_COMPILE_BASE}
    fi

    # Build base image with cache.
    _stop_existing_docker_container_by_name ${CONTAINER_NAME}
    _remove_container ${CONTAINER_NAME}
    _is_image_existing ${BASE_IMAGE_NAME_WITH_CACHE} ${IMAGE_TAG_COMPILE_BASE_WITH_CACHE}
    if [[ $? == 1 ]]; then
        echo_info "Docker image ${BASE_IMAGE_NAME_WITH_CACHE}:${IMAGE_TAG_COMPILE_BASE_WITH_CACHE} already exists."
        _remove_docker_image ${BASE_IMAGE_NAME_WITH_CACHE} ${IMAGE_TAG_COMPILE_BASE_WITH_CACHE}
    fi
    _start_docker_container ${BASE_IMAGE_NAME} ${IMAGE_TAG_COMPILE_BASE}
    _commit_container ${CONTAINER_NAME} ${BASE_IMAGE_NAME} ${IMAGE_TAG_COMPILE_BASE_WITH_CACHE}
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
