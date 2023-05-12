#!/bin/bash

# Deploy a master node of Kubernetes.

OUTPUT_FOR_OTHERS='/dev/null'
PATH_K8_DATA_ROOT="${HOME}/data/kubernetes"
PATH_K8_ETCD="${PATH_K8_DATA_ROOT}/etcd"
PATH_YAML_K8ADM_INIT="${PATH_K8_DATA_ROOT}/kubeadm_init.yaml"
PATH_YAML_CREATE_CLUSTER_ROLE_BINDING="${PATH_K8_DATA_ROOT}/create_cluster_role_binding.yaml"
PATH_YAML_CREATE_SERVICE_ACCOUNT="${PATH_K8_DATA_ROOT}/create_service_account.yaml"
PATH_YAML_K8ADM_INIT="${PATH_K8_DATA_ROOT}/kubeadm_init.yaml"
VERSION_APT_TRANSPORT_HTTPS='2.0.9'
VERSION_CONTAINERD_IO='1.6.21-1'
VERSION_CURL='7.68.0-1ubuntu2.18'
VERSION_KUBEADM='1.27.1'
VERSION_KUBECTL='1.27.1'
VERSION_KUBELET='1.27.1'
VERSION_KUBERNETES='1.26.0'
YAML_URL_DASHBORD='https://raw.githubusercontent.com/kubernetes/dashboard/v2.7.0/aio/deploy/recommended.yaml'
YAML_URL_WEAVE='https://github.com/weaveworks/weave/releases/download/v2.8.1/weave-daemonset-k8s.yaml'
YAML_STR_CREATE_SERVICE_ACCOUNT='''apiVersion: v1
kind: ServiceAccount
metadata:
  name: admin-user
  namespace: kubernetes-dashboard'''
YAML_STR_CREATE_CLUSTER_ROLE_BINDING='''apiVersion: rbac.authorization.k8s.io/v1
kind: ClusterRoleBinding
metadata:
  name: admin-user
roleRef:
  apiGroup: rbac.authorization.k8s.io
  kind: ClusterRole
  name: cluster-admin
subjects:
- kind: ServiceAccount
  name: admin-user
  namespace: kubernetes-dashboard'''
YAML_STR_KUBEADM_INIT="""apiVersion: kubeadm.k8s.io/v1beta3
kind: ClusterConfiguration
kubernetesVersion: ${VERSION_KUBERNETES}

apiServer:
  extraArgs:
    runtime-config: \"api/all=true\"

etcd:
  local:
    dataDir: ${PATH_K8_ETCD}"""

clean_up()
{
    echo "clean up ..."
    which kubeadm > ${OUTPUT_FOR_OTHERS} && kubeadm reset -f > ${OUTPUT_FOR_OTHERS}
    [[ -e "${HOME}/.kube" ]] && rm -rf "${HOME}/.kube"
    echo "clean up done"
}

prepare_for_installing()
{
    echo "preparing for installing ..."
    [[ -d /etc/apt/keyrings ]] || sudo mkdir -p /etc/apt/keyrings
    [[ -f /etc/apt/keyrings/docker.gpg ]] && sudo rm -f /etc/apt/keyrings/docker.gpg
    curl -fsSL https://download.docker.com/linux/ubuntu/gpg | gpg --dearmor -o /etc/apt/keyrings/docker.gpg

    echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] https://download.docker.com/linux/ubuntu $(lsb_release -cs) stable" \
        | tee /etc/apt/sources.list.d/docker.list > ${OUTPUT_FOR_OTHERS}

    echo "preparing for installing done"
}

apt_install()
{
    echo "apt update ..."
    apt update > ${OUTPUT_FOR_OTHERS} 2>&1

    echo "apt install ..."
    apt install -y \
        apt-transport-https=${VERSION_APT_TRANSPORT_HTTPS} \
        curl=${VERSION_CURL} \
        containerd.io=${VERSION_CONTAINERD_IO} \
        kubeadm=${VERSION_KUBEADM} \
        kubectl=${VERSION_KUBECTL} \
        kubelet=${VERSION_KUBELET} \
        > ${OUTPUT_FOR_OTHERS} 2&>1

    echo "apt hold ..."
    apt-mark hold apt-transport-https curl containerd.io > ${OUTPUT_FOR_OTHERS}

    echo "apt done"
}

set_configs()
{
    [[ -e /etc/containerd ]] && rm -rf /etc/containerd
    mkdir -p /etc/containerd
    containerd config default > /etc/containerd/config.toml

    echo "SystemdCgroup = true" > ttt.toml
    cat /etc/containerd/config.toml >> ttt.toml
    sed -i 's/SystemdCgroup = false/SystemdCgroup = true/g' ttt.toml
    mv ttt.toml /etc/containerd/config.toml

    systemctl restart containerd > ${OUTPUT_FOR_OTHERS}
    swapoff -a > ${OUTPUT_FOR_OTHERS}

    modprobe br_netfilter
    bash -c 'echo 1 > /proc/sys/net/ipv4/ip_forward'

    [[ -d ${PATH_K8_DATA_ROOT} ]] || mkdir -p ${PATH_K8_DATA_ROOT}
    echo "${YAML_STR_KUBEADM_INIT}" > ${PATH_YAML_K8ADM_INIT}
}

kubeadm_init()
{
    echo "kubeadm init --config ${PATH_YAML_K8ADM_INIT}"
    kubeadm init --config ${PATH_YAML_K8ADM_INIT}

    mkdir -p ${HOME}/.kube
    cp /etc/kubernetes/admin.conf ${HOME}/.kube/config
    chown $(id -u):$(id -g) ${HOME}/.kube/config
}

kubectl_create_applications()
{
    echo -e "\nkubectl apply -f ${YAML_URL_WEAVE}"
    kubectl apply -f ${YAML_URL_WEAVE}

    echo -e "\nkubectl apply -f ${YAML_URL_DASHBORD}"
    kubectl apply -f ${YAML_URL_DASHBORD}
    kubectl proxy --address='0.0.0.0' --port=8001 --accept-hosts='.*' &
    kubectl port-forward -n kubernetes-dashboard --address 0.0.0.0 service/kubernetes-dashboard 8080:443 &

    echo "${YAML_STR_CREATE_SERVICE_ACCOUNT}" > ${PATH_YAML_CREATE_SERVICE_ACCOUNT}
    echo -e "\nkubectl apply -f ${PATH_YAML_CREATE_SERVICE_ACCOUNT}"
    kubectl apply -f ${PATH_YAML_CREATE_SERVICE_ACCOUNT}

    echo "${YAML_STR_CREATE_CLUSTER_ROLE_BINDING}" > ${PATH_YAML_CREATE_CLUSTER_ROLE_BINDING}
    echo -e "\nkubectl apply -f ${PATH_YAML_CREATE_CLUSTER_ROLE_BINDING}"
    kubectl apply -f ${PATH_YAML_CREATE_CLUSTER_ROLE_BINDING}

    kubectl -n kubernetes-dashboard create token admin-user

    # kubectl -n kubernetes-dashboard delete serviceaccount admin-user
    # kubectl -n kubernetes-dashboard delete clusterrolebinding admin-user
}

show_k8_pods()
{
    echo -e "\n\n\nkubectl get pods -A"

    kubectl get pods -A
}

clean_up
prepare_for_installing
apt_install
set_configs
kubeadm_init
kubectl_create_applications

show_k8_pods