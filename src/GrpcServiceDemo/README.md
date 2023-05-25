# GRPC Service demo

## How to run in local

```dotnetcli
dotnet run [-- [--with-http]]
```

```--with-http``` is optional, if you want to run the server without TLS.

## How to run as a docker container

### Release image

Build docker image by dockerfile for development:

```docker

```docker
docker build -t grpc-service-demo-image:debug -f Dockerfile.Development ../../
```

Build docker image by dockerfile for release:

```docker

```docker
docker build -t grpc-service-demo-image -f Dockerfile ../../
```

For release as a debug image:

```dotnetcli
dotnet publish ./GrpcServiceDemo.csproj \
    --configuration Debug \
    -p:PublishProfile=DefaultContainer \
    -p:ContainerBaseImage=mcr.microsoft.com/dotnet/sdk:7.0 \
    -p:ContainerRuntimeIdentifier=linux-x64 \
    -p:ContainerImageName=grpc-service-demo-image \
    -p:ContainerImageTags=latest \
    -p:ContainerWorkingDirectory=/GrpcServiceDemo
```

For release as a extremely small image:

```dotnetcli
dotnet publish ./GrpcServiceDemo.csproj \
    --self-contained \
    --configuration Release \
    -p:PublishProfile=DefaultContainer \
    -p:ContainerBaseImage=mcr.microsoft.com/dotnet/nightly/runtime-deps:7.0-jammy-chiseled \
    -p:ContainerRuntimeIdentifier=linux-x64 \
    -p:ContainerImageName=grpc-service-demo-image \
    -p:ContainerImageTags=latest \
    -p:InvariantGlobalization=true \
    -p:PublishSingleFile=true \
    -p:PublishTrimmed=true \
    -p:ContainerWorkingDirectory=/GrpcServiceDemo
```

### Run the image

Run for debug:

```docker
docker run --rm -it -p 7263:7263 --volume=/root/work/ShootingStar/:/ShootingStar --entrypoint "bash" --name GrpcServiceDemo grpc-service-demo-image
```

Run normally:

```docker
docker run --rm -dp 7263:7263 --name GrpcServiceDemo grpc-service-demo-image [--with-http]
```
