# Here are the common variables & functions for all bash scripts.

# color codes
COLOR_RED='\033[0;31m'
COLOR_GREEN='\033[0;32m'
COLOR_YELLOW='\033[0;33m'
COLOR_BLUE='\033[0;34m'
COLOR_MAGENTA='\033[0;35m'
COLOR_END='\033[0m'

# Colorized echo functions.
echo_info() {
    echo -e "${COLOR_GREEN}$*${COLOR_END}"
}

echo_warning() {
    echo -e "${COLOR_YELLOW}$*${COLOR_END}"
}

echo_error() {
    echo -e "${COLOR_RED}$*${COLOR_END}"
}

################################################################################
# Common functions for Docker operations
################################################################################

DOCKER_CMD='docker'

check_docker_is_available() {
    ${DOCKER_CMD} info > /dev/null 2>&1
    if [[ $? != 0 ]]; then
        echo -e "${COLOR_RED}Docker is not installed or the deamon is not running.${COLOR_END}"
        exit 2
    fi
}

check_kubectl_is_available() {
    kubectl version --client > /dev/null 2>&1
    if [[ $? != 0 ]]; then
        echo_error "kubectl is not installed or not available in PATH."
        exit 2
    fi
}

build_docker_image() {
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

pull_docker_image() {
    image_name=$1
    image_tag=$2
    echo_info "Pulling Docker image ${image_name}:${image_tag}..."
    echo_warning "${DOCKER_CMD} pull ${image_name}:${image_tag}"

    ${DOCKER_CMD} pull ${image_name}:${image_tag}

    if [[ $? != 0 ]]; then
        echo_error "Failed to pull Docker image ${image_name}:${image_tag}."
        exit 3
    fi

    echo_info "Docker image ${image_name}:${image_tag} pulled successfully."
}

remove_docker_image() {
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

is_image_existing() {
    image_name=$1
    image_tag=$2

    result=$(${DOCKER_CMD} images --all --format table | sed -E 's/^[[:space:]]+//; s/[[:space:]]+/ /g' | grep -- "${image_name} ${image_tag}" | wc -l)
    result=$(echo "${result}" | tr -d '[:space:]')
    if (( result == 0 )); then
        return 0
    fi

    return 1
}

stop_existing_docker_container_by_image() {
    image_name=$1
    image_tag=$2
    result=$(${DOCKER_CMD} container list --all | grep -- "${image_name}:${image_tag}" | wc -l)
    result=$(echo "${result}" | tr -d '[:space:]')
    if (( result == 0 )); then
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
        result=$(echo "${result}" | tr -d '[:space:]')
        if (( result == 0 )); then
            return
        fi

    done

    echo_info "Docker container stopped successfully."
}

stop_existing_docker_container_by_name() {
    container_name=$1
    result=$(${DOCKER_CMD} container list --all | grep -- "${container_name}" | wc -l)
    result=$(echo "${result}" | tr -d '[:space:]')
    if (( result == 0 )); then
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
        result=$(echo "${result}" | tr -d '[:space:]')
        if (( result == 0 )); then
            return
        fi

    done

    echo_info "Docker container stopped successfully."
}

remove_container() {
    container_name=$1
    result=$(${DOCKER_CMD} container list --all | grep -- "${container_name}" | wc -l)
    result=$(echo "${result}" | tr -d '[:space:]')
    if (( result == 0 )); then
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

commit_container() {
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

    echo_info "Docker container ${container_name} committed as ${image_name}:${image_tag} successfully."
}
