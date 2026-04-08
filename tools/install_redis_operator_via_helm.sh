#!/usr/bin/env bash
set -euo pipefail

NAMESPACE="redis-operators"

kubectl get namespace "${NAMESPACE}" >/dev/null 2>&1 || kubectl create namespace "${NAMESPACE}"

helm repo add ot-helm https://ot-container-kit.github.io/helm-charts/ >/dev/null 2>&1 || true
helm repo update

helm upgrade --install redis-operator ot-helm/redis-operator \
  --namespace "${NAMESPACE}" \
  --create-namespace \
  --set featureGates.GenerateConfigInInitContainer=true

echo
echo "=== Pods ==="
kubectl get pods -n "${NAMESPACE}"

echo
echo "=== Deployment ==="
kubectl get deploy -n "${NAMESPACE}"

echo
echo "=== CRDs ==="
kubectl get crd | grep opstreelabs || true
