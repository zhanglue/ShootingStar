#!/bin/bash

REPO_ROOT_PATH=$(git rev-parse --show-toplevel)
DOCKER_FILES_ROOT_PATH="${REPO_ROOT_PATH}/ci_cd/docker_files"
. ${REPO_ROOT_PATH}/tools/common.sh

DOCKER_FILE_TO_IMAGE_NAME=(
    'clients/elasticsearch_client                   | elasticsearch-client'
    'clients/redis_client                           | redis-client'
    'recommendation_engine/gateway                  | recommendation-engine-gateway'
    'recommendation_engine/profile                  | recommendation-engine-profile'
    'recommendation_engine/ranking                  | recommendation-engine-ranking'
    'recommendation_engine/retrieval_orchestrator   | recommendation-engine-retrieval-orchestrator'
    'recommendation_engine/retriever_item_cf        | recommendation-engine-retriever-item-cf'
    'recommendation_engine/retriever_user_cf        | recommendation-engine-retriever-user-cf'
    'clients/recommendation_engine_client           | recommendation-engine-client'
    'clients/recommendation_engine_profile_client   | recommendation-engine-profile-client'
    'clients/recommendation_engine_retrieval_client | recommendation-engine-retrieval-client'
    'clients/recommendation_engine_retriever_client | recommendation-engine-retriever-client'
    'weather_forecast/fetcher                       | weather-forecast-fetcher'
    'clients/weather_forecast_fetcher_client        | weather-forecast-fetcher-client'
)
namespace=""
image_tag="latest"
image_registry="192.168.1.101:55000"
assume_yes="false"
skip_build="false"
skip_tag="false"
skip_push="false"

compose_registry_image_name() {
    image_name=$1
    normalized_image_registry="${image_registry%/}"
    echo "${normalized_image_registry}/${image_name}"
}

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
        build_docker_image "${dockerfile_path}" "${image_name}" "${image_tag}"
    done
}

tag_target_docker_images() {
    if [[ -z "${image_registry}" ]]; then
        return
    fi

    for config in "${DOCKER_FILE_TO_IMAGE_NAME[@]}"; do
        image_name=$(echo "${config}" | cut -d '|' -f 2 | sed 's/^[[:space:]]*//')
        if [[ -n "${namespace}" && ! "${image_name}" =~ ^${namespace}-.+ ]]; then
            continue
        fi

        registry_image_name=$(compose_registry_image_name "${image_name}")
        echo
        echo_info "Tag docker image ${image_name}:${image_tag} as ${registry_image_name}:${image_tag}..."
        echo_warning "${DOCKER_CMD} tag ${image_name}:${image_tag} ${registry_image_name}:${image_tag}"

        ${DOCKER_CMD} tag "${image_name}:${image_tag}" "${registry_image_name}:${image_tag}"

        if [[ $? != 0 ]]; then
            echo_error "Failed to tag Docker image ${image_name}:${image_tag} as ${registry_image_name}:${image_tag}."
            exit 5
        fi

        echo_info "Docker image ${registry_image_name}:${image_tag} tagged successfully."
    done
}

confirm_push_target_docker_images() {
    if [[ "${assume_yes}" == "true" ]]; then
        return 0
    fi

    echo
    read -r -p "Docker images will be pushed to ${image_registry}. Continue? [y/N] " answer
    case "${answer}" in
        y | Y | yes | YES)
            return 0
            ;;
        *)
            echo_warning "Skipping Docker image push."
            return 1
            ;;
    esac
}

push_target_docker_images() {
    if [[ -z "${image_registry}" ]]; then
        return
    fi

    confirm_push_target_docker_images
    if [[ $? != 0 ]]; then
        return
    fi

    for config in "${DOCKER_FILE_TO_IMAGE_NAME[@]}"; do
        image_name=$(echo "${config}" | cut -d '|' -f 2 | sed 's/^[[:space:]]*//')
        if [[ -n "${namespace}" && ! "${image_name}" =~ ^${namespace}-.+ ]]; then
            continue
        fi

        registry_image_name=$(compose_registry_image_name "${image_name}")
        echo
        echo_info "Push docker image ${registry_image_name}:${image_tag}..."
        echo_warning "${DOCKER_CMD} push ${registry_image_name}:${image_tag}"

        ${DOCKER_CMD} push "${registry_image_name}:${image_tag}"

        if [[ $? != 0 ]]; then
            echo_error "Failed to push Docker image ${registry_image_name}:${image_tag}."
            exit 6
        fi

        echo_info "Docker image ${registry_image_name}:${image_tag} pushed successfully."
    done
}

list_target_docker_images() {
    echo
    echo "Target docker images:"
    for config in "${DOCKER_FILE_TO_IMAGE_NAME[@]}"; do
        image_name=$(echo "${config}" | cut -d '|' -f 2 | sed 's/^[[:space:]]*//')
        if [[ -n "${namespace}" && ! "${image_name}" =~ ^${namespace}-.+ ]]; then
            continue
        fi

        echo "  * ${image_name}:${image_tag}"
    done

    if [[ -z "${image_registry}" ]]; then
        return
    fi

    for config in "${DOCKER_FILE_TO_IMAGE_NAME[@]}"; do
        image_name=$(echo "${config}" | cut -d '|' -f 2 | sed 's/^[[:space:]]*//')
        if [[ -n "${namespace}" && ! "${image_name}" =~ ^${namespace}-.+ ]]; then
            continue
        fi

        registry_image_name=$(compose_registry_image_name "${image_name}")
        echo "  * ${registry_image_name}:${image_tag}"
    done
}

_main_flow() {
    builtin cd ${REPO_ROOT_PATH}

    check_docker_is_available
    if [[ "${skip_build}" == "true" ]]; then
        echo_warning "Skipping Docker image build."
    else
        check_target_docker_images_are_not_in_use
        remove_target_docker_images
        build_target_docker_images
    fi

    if [[ "${skip_tag}" == "true" ]]; then
        echo_warning "Skipping Docker image tag."
    else
        tag_target_docker_images
    fi

    if [[ "${skip_push}" == "true" ]]; then
        echo_warning "Skipping Docker image push."
    else
        push_target_docker_images
    fi
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
        -r | --registry | --image-registry)
            shift
            image_registry=$1
            ;;
        -y | --yes)
            assume_yes="true"
            ;;
        --skip-build)
            skip_build="true"
            ;;
        --skip-tag)
            skip_tag="true"
            ;;
        --skip-push)
            skip_push="true"
            ;;
        *)
            echo_error "Unknown argument: $1"
            exit 1
            ;;
    esac
    shift
done

_main_flow
