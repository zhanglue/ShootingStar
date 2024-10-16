#!/bin/bash

# It is a script translated from the task in VS Code task.json.

. common.sh

# global variables
REPO_ROOT_PATH=$(git rev-parse --show-toplevel)
FLAG_BUILD_ONLY='false'
FLAG_FORECE_REBUILD='false'
FLAG_IS_PROD='false'
ARGS=''

# must be set in the setup function
DOCKERFILE_PATH=''
IMAGE_NAME=''
IMAGE_TAG=''
DOCKER_CONTAINER_RUN_CMD=''
DOCKER_CONTAINER_NAME=''
DOCKER_CONTAINER_PORT=''

_check_docker_is_available() {
    docker info > /dev/null 2>&1
    if [[ $? != 0 ]]; then
        echo -e "${COLOR_RED}Docker is not installed or the deamon is not running.${COLOR_END}"
        exit 2
    fi
}

_build_docker_image() {
    echo_info "Build docker image ${IMAGE_NAME}:${IMAGE_TAG}..."
    echo_warning "docker build -t ${IMAGE_NAME}:${IMAGE_TAG} -f ${DOCKERFILE_PATH} ${REPO_ROOT_PATH}"

    docker build -t ${IMAGE_NAME}:${IMAGE_TAG} -f ${DOCKERFILE_PATH} ${REPO_ROOT_PATH}

    if [[ $? != 0 ]]; then
        echo_error "Failed to build Docker image ${IMAGE_NAME}:${IMAGE_TAG}."
        exit 3
    fi

    echo_info "Docker image ${IMAGE_NAME}:${IMAGE_TAG} built successfully."
}

_remove_docker_image() {
    echo_info "Removing existing Docker image ${IMAGE_NAME}:${IMAGE_TAG}..."
    echo_warning "docker rmi ${IMAGE_NAME}:${IMAGE_TAG}"

    docker rmi ${IMAGE_NAME}:${IMAGE_TAG}

    if [[ $? != 0 ]]; then
        echo_error "Failed to remove existing Docker image ${IMAGE_NAME}:${IMAGE_TAG}."
        exit 4
    fi

    echo_info "Docker image ${IMAGE_NAME}:${IMAGE_TAG} removed successfully."
}

_stop_existing_docker_container_by_image() {
    result=$(docker container list --all | grep -- "${IMAGE_NAME}:${IMAGE_TAG}" | wc -l)
    if [[ $result == 0 ]]; then
        return
    fi

    try_count=5
    while [[ $try_count > 0 ]]; do
        echo_info "Stopping existing Docker container with image ${IMAGE_NAME}:${IMAGE_TAG}..."

        container_id=$(docker container list --all | grep -- "${IMAGE_NAME}:${IMAGE_TAG}" | head -n 1 | sed 's/[[:space:]]\+/ /g' | cut -d ' ' -f 1)

        echo_warning "docker container stop ${container_id}"
        docker container stop ${container_id}

        if [[ $? == 0 ]]; then
            break
        fi

        try_count=$((try_count - 1))
        sleep 3s

        result=$(docker container list --all | grep -- "${IMAGE_NAME}:${IMAGE_TAG}" | wc -l)
        if [[ $result == 0 ]]; then
            return
        fi

    done

    echo_info "Docker container stopped successfully."
}

_check_if_image_exists_or_build() {
    result=$(docker images --all | sed 's/[[:space:]]\+/ /g' | grep -- "${IMAGE_NAME} ${IMAGE_TAG}" | wc -l)
    if [[ $result > 0 ]]; then
        if [[ $FLAG_FORECE_REBUILD == "false" ]]; then
            echo_info "Docker image ${IMAGE_NAME}:${IMAGE_TAG} already exists and skip to rebuild."
            return
        else
            echo_info "Docker image ${IMAGE_NAME}:${IMAGE_TAG} already exists, remove it first."
            _stop_existing_docker_container_by_image
            _remove_docker_image
        fi
    else
        echo_info "No existing Docker image ${IMAGE_NAME}:${IMAGE_TAG} found."
    fi

    _build_docker_image

    if [[ $FLAG_BUILD_ONLY == true ]]; then
        exit 0
    fi
}

_stop_existing_docker_container_by_name() {
    result=$(docker container list --all | grep -- "${DOCKER_CONTAINER_NAME}" | wc -l)
    if [[ $result == 0 ]]; then
        return
    fi

    try_count=3
    while [[ $try_count > 0 ]]; do
        echo_info "Stopping existing Docker container ${DOCKER_CONTAINER_NAME}..."
        echo_warning "docker container stop ${DOCKER_CONTAINER_NAME}"

        docker container stop ${DOCKER_CONTAINER_NAME}

        if [[ $? == 0 ]]; then
            break
        fi

        try_count=$((try_count - 1))
        sleep 3s

        result=$(docker container list --all | grep -- "${DOCKER_CONTAINER_NAME}" | wc -l)
        if [[ $result == 0 ]]; then
            return
        fi

    done

    echo_info "Docker container stopped successfully."
}

_start_docker_container() {
    echo_info "Starting Docker container ${IMAGE_NAME}:${IMAGE_TAG} with command..."
    echo_warning "${DOCKER_CONTAINER_RUN_CMD}"
    ${DOCKER_CONTAINER_RUN_CMD}
    if [[ $? != 0 ]]; then
        echo_error "Failed to start docker container with image ${IMAGE_NAME}:${IMAGE_TAG}."
        exit 4
    fi
    echo_info "Docker container started successfully."
}

_main_flow() {
    _check_docker_is_available
    _check_if_image_exists_or_build
    _stop_existing_docker_container_by_name
    _start_docker_container
}

_show_config() {
    echo -e "${COLOR_MAGENTA}################################################################################${COLOR_END}"
    echo -e "${COLOR_YELLOW}FLAG_IS_PROD              ${COLOR_END}: ${FLAG_IS_PROD}"
    echo -e "${COLOR_YELLOW}FLAG_BUILD_ONLY           ${COLOR_END}: ${FLAG_BUILD_ONLY}"
    echo -e "${COLOR_YELLOW}FLAG_FORECE_REBUILD       ${COLOR_END}: ${FLAG_FORECE_REBUILD}"
    echo -e "${COLOR_YELLOW}DOCKERFILE_PATH           ${COLOR_END}: ${DOCKERFILE_PATH}"
    echo -e "${COLOR_YELLOW}IMAGE_NAME                ${COLOR_END}: ${IMAGE_NAME}"
    echo -e "${COLOR_YELLOW}IMAGE_TAG                 ${COLOR_END}: ${IMAGE_TAG}"
    echo -e "${COLOR_YELLOW}DOCKER_CONTAINER_RUN_CMD  ${COLOR_END}: ${DOCKER_CONTAINER_RUN_CMD}"
    echo -e "${COLOR_YELLOW}DOCKER_CONTAINER_NAME     ${COLOR_END}: ${DOCKER_CONTAINER_NAME}"
    echo -e "${COLOR_YELLOW}DOCKER_CONTAINER_PORT     ${COLOR_END}: ${DOCKER_CONTAINER_PORT}"
    echo -e "${COLOR_MAGENTA}################################################################################${COLOR_END}"
}

_setup_for_grpc_service_demo() {
    if [[ $FLAG_IS_PROD == "true" ]]; then
        DOCKERFILE_PATH="${REPO_ROOT_PATH}/src/GrpcServiceDemo/Dockerfile"
        IMAGE_TAG="latest"
    else
        DOCKERFILE_PATH="${REPO_ROOT_PATH}/src/GrpcServiceDemo/Dockerfile.Development"
        IMAGE_TAG="debug"
    fi

    IMAGE_NAME="grpc-service-demo"
    DOCKER_CONTAINER_NAME="GrpcServiceDemo"
    DOCKER_CONTAINER_PORT="7263"
    ARGS="--with-http"

    tmp="docker container run --detach --rm"
    tmp="${tmp} --name ${DOCKER_CONTAINER_NAME}"
    tmp="${tmp} --publish ${DOCKER_CONTAINER_PORT}:${DOCKER_CONTAINER_PORT}"
    tmp="${tmp} ${IMAGE_NAME}:${IMAGE_TAG} ${ARGS}"
    DOCKER_CONTAINER_RUN_CMD="${tmp}"
}

while [[ "$#" -gt 0 ]]; do
    case $1 in
        --prod)
            FLAG_IS_PROD="true"
            shift
            ;;
        -b | --build)
            FLAG_BUILD_ONLY="true"
            shift
            ;;
        -f | --force-rebuild)
            FLAG_FORECE_REBUILD="true"
            shift
            ;;
        *)
            ARGS="${ARGS} $1"
            ;;
    esac
    shift
done

_setup_for_grpc_service_demo
_show_config
_main_flow
