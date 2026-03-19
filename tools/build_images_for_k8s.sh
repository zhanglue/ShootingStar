#!/bin/bash

REPO_ROOT_PATH=$(git rev-parse --show-toplevel)
DOCKER_FILES_ROOT_PATH="${REPO_ROOT_PATH}/ci_cd/docker_files"
. ${REPO_ROOT_PATH}/tools/common.sh

DOCKER_FILE_TO_IMAGE_NAME=(
    'recommendation_engine/gateway                | recommendation-engine-gateway'
    'recommendation_engine/profile                | recommendation-engine-profile'
    'weather_forecast/fetcher                     | weather-forecast-fetcher'
    'clients/recommendation_engine_client         | recommendation-engine-client'
    'clients/recommendation_engine_profile_client | recommendation-engine-profile-client'
    'clients/weather_forecast_fetcher_client      | weather-forecast-fetcher-client'
)
namespace=""
image_tag="latest"

check_target_docker_images_are_not_in_use() {
    for config in "${DOCKER_FILE_TO_IMAGE_NAME[@]}"; do
        image_name=$(echo "${config}" | cut -d '|' -f 2 | sed 's/^[[:space:]]*//')
        if [[ -n "${namespace}" && ! "${image_name}" =~ ^${namespace}-.+ ]]; then
            continue
        fi

        is_docker_image_in_use "${image_name}" "${image_tag}"
        if [[ $? != 0 ]]; then
            echo_error "Docker image ${image_name}:${image_tag} is still being used by at least one container. Stop and remove those containers before rebuilding."
            exit 4
        fi
    done
}

remove_target_docker_images() {
    for config in "${DOCKER_FILE_TO_IMAGE_NAME[@]}"; do
        image_name=$(echo "${config}" | cut -d '|' -f 2 | sed 's/^[[:space:]]*//')
        if [[ -n "${namespace}" && ! "${image_name}" =~ ^${namespace}-.+ ]]; then
            continue
        fi

        is_image_existing "${image_name}" "${image_tag}"
        if [[ $? != 0 ]]; then
            remove_docker_image "${image_name}" "${image_tag}"
        fi
    done
}

build_target_docker_images() {
    for config in "${DOCKER_FILE_TO_IMAGE_NAME[@]}"; do
        dockerfile_relative_path=$(echo "${config}" | cut -d '|' -f 1 | sed 's/[[:space:]]*$//')
        image_name=$(echo "${config}" | cut -d '|' -f 2 | sed 's/^[[:space:]]*//')
        if [[ -n "${namespace}" && ! "${image_name}" =~ ^${namespace}-.+ ]]; then
            echo_info "Skipping image ${image_name}: it does not match namespace prefix ${namespace}-."
            continue
        fi

        dockerfile_path="${DOCKER_FILES_ROOT_PATH}/${dockerfile_relative_path}"
        echo
        build_docker_image ${dockerfile_path} ${image_name} ${image_tag}
    done
}

list_target_docker_images() {
    echo
    echo_info "All docker images following have been built successfully:"
    for config in "${DOCKER_FILE_TO_IMAGE_NAME[@]}"; do
        image_name=$(echo "${config}" | cut -d '|' -f 2 | sed 's/^[[:space:]]*//')
        if [[ -n "${namespace}" && ! "${image_name}" =~ ^${namespace}-.+ ]]; then
            continue
        fi

        echo_info "  * ${image_name}:${image_tag}"
    done
}

_main_flow() {
    builtin cd ${REPO_ROOT_PATH}

    check_docker_is_available
    check_target_docker_images_are_not_in_use
    remove_target_docker_images
    build_target_docker_images
    list_target_docker_images
}

while [[ "$#" -gt 0 ]]; do
    case $1 in
        --docker-cmd)
            shift
            DOCKER_CMD=$1
            ;;
        -n | --namespace)
            shift
            namespace=$1
            ;;
        -t | --image-tag)
            shift
            image_tag=$1
            ;;
        *)
            echo_error "Unknown argument: $1"
            exit 1
            ;;
    esac
    shift
done

_main_flow
