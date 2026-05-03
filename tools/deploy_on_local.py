echo "################################################################################"
echo "Profile"
bazel run //src/recommendation_engine/profile:profile_bin -- \
    --config_path=/Volumes/DataBase/Work/ShootingStar/src/recommendation_engine/profile/config.debug.yaml \
    > logs/profile.log &
sleep 3s
bazel run //src/clients:recommendation_engine_profile_client > logs/response_profile.txt

echo ""
echo "################################################################################"
echo "RetriverItemCF"
bazel run //src/recommendation_engine/retrieval/retrievers/item_cf:retriever_item_cf_bin -- \
    --config_path=/Volumes/DataBase/Work/ShootingStar/src/recommendation_engine/retrieval/retrievers/item_cf/config.debug.yaml \
    > logs/retriever_item_cf.log &
sleep 3s
bazel run //src/clients:recommendation_engine_retriever_client -- -p 50210 > logs/response_retriever_item_cf.log

echo ""
echo "################################################################################"
echo "RetriverUserCF"
bazel run //src/recommendation_engine/retrieval/retrievers/user_cf:retriever_user_cf_bin -- \
    --config_path=/Volumes/DataBase/Work/ShootingStar/src/recommendation_engine/retrieval/retrievers/user_cf/config.debug.yaml \
    > logs/retriever_user_cf.log &
sleep 3s
bazel run //src/clients:recommendation_engine_retriever_client -- -p 50220 > logs/response_retriever_user_cf.log

echo ""
echo "################################################################################"
echo "RetrievalOrchestrator"
bazel run //src/recommendation_engine/retrieval/orchestrator:retrieval_orchestrator_bin -- \
    --config_path=/Volumes/DataBase/Work/ShootingStar/src/recommendation_engine/retrieval/orchestrator/config.debug.yaml \
    > logs/retrieval_orchestrator.log &
sleep 3s
bazel run //src/clients:recommendation_engine_retrieval_client > logs/response_retrieval.log

echo ""
echo "################################################################################"
echo "Gateway"
bazel run //src/recommendation_engine/gateway:gateway_bin -- \
    --config_path=/Volumes/DataBase/Work/ShootingStar/src/recommendation_engine/gateway/config.debug.yaml \
    > logs/gateway.log &
sleep 3s
bazel run //src/clients:recommendation_engine_client -- -u 300 -u 669 -u 906 > logs/response_gateway.log
