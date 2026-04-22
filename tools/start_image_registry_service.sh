#!/bin/bash

# Start a local image registry service using Podman.
# The registry will be accessible on port 5000 and will store images in the specified directory.
# It will automatically restart if it crashes or the system reboots.
# Execute it on 192.168.1.101 so that other machines in the network can access it.
# Tag and push images to this registry using the address 192.168.1.101:5000.
# For example, to tag an image: `podman tag my-image:latest 192.168.1.101:5000/my-image:latest`

# Caution:
# It starts a HTTP service.
# Configure the docker/podman/k3s cluster to allow insecure registries.

sudo podman run -d \
  --name local-image-registry \
  --restart=always \
  -p 55000:5000 \
  -v /home/zhanglue/work/image_registry:/var/lib/registry \
  docker.io/library/registry:3.1.0
