ES_PASSWORD=$(kubectl get secret -n recommendation-engine-es item-index-es-elastic-user \
  -o go-template='{{.data.elastic | base64decode}}')

kubectl create secret generic item-index-es-elastic-user \
  -n recommendation-engine \
  --from-literal=elastic="${ES_PASSWORD}" \
  --dry-run=client -o yaml | kubectl apply -f -

kubectl get secret -n recommendation-engine-es item-index-es-http-certs-public \
  -o go-template='{{index .data "ca.crt" | base64decode}}' \
  > /tmp/shootingstar-es-ca.crt

kubectl create secret generic item-index-es-http-certs-public \
  -n recommendation-engine \
  --from-file=ca.crt=/tmp/shootingstar-es-ca.crt \
  --dry-run=client -o yaml | kubectl apply -f -

REDIS_PASSWORD=$(kubectl get secret -n recommendation-engine-redis redis-auth \
  -o go-template='{{.data.password | base64decode}}')

kubectl create secret generic redis-auth \
  -n recommendation-engine \
  --from-literal=password="${REDIS_PASSWORD}" \
  --dry-run=client -o yaml | kubectl apply -f -
