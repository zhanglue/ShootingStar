# GRPC Service demo

## Details in the code

The endpoints could be defined in the appsettings.json file, too.
Such as following:

``` json
"Kestrel": {
  "Endpoints": {
    "ForRESTful": {
        "Url": "http://localhost:7262",
        "Protocols": "Http1"
    },
    "ForGRPC": {
        "Url": "https://localhost:7263",
        "Protocols": "Http2"
    }
  },
 "EndpointDefaults": {
    "Protocols": "Http1AndHttp2"
  },
 "Certificates": {
    "Default": {
      "Path": "localhost.pfx",
      "Password": "123456",
      "AllowInvalid": "false"
    }
  }
}
```

1. The URL in config file specified the schema (HTTP or HTTPS) and the port number at the same time.

   It will conflict if define the endpoints both in the appsettings.json file and the code.

   So, we just define the endpoints in the code for more flexible.

2. As usual, it is not necessary to define the endpoints both of protocols Http1 and Http2.

   Define the endpoint of Http1 is enough for RESTful APIs, while define the endpoint of Http2 is sufficient for gRPC APIs.

   It is a little bit of weird to define the endpoints both of protocols Http1 and Http2,

   while it means that the service will expose both RESTful APIs and gRPC APIs at the same time.

   The protocols is not required for the endpoints, it will be set to Http1AndHttp2 by ```EndpointsDefaults.Protocols```.

   In this case, the HTTPS is force to be enabled, and the HTTP is disabled.

3. The certificate is not required for the HTTP endpoints.

   It is possible to use various certificates for different HTTPS endpoints by specifying the details in endpoint.

   If not specify the certificate for the endpoint, the default certificate will be used.

## How to run in local

```dotnetcli
dotnet run . [-- [--with-http] [--redirect-to-https] [--cert-path <path>] [--cert-password <password>]]
```

```--with-http``` is an optional flag, which means both RESTful APIs and gRPC APIs will be exposed as HTTP.

```--redirect-to-https``` is an optional flag, which means that the requests to the HTTP port will be redirected to the HTTPS port.

```--cert-path``` is an optional flag, which means the path of the certificate file.

```--cert-password``` is an optional flag, which means the password of the certificate file.

```--with-http``` and ```--redirect-to-https``` are mutually exclusive.

### Example I

```dotnetcli
dotnet run . -- --with-http
```

It exposes ```http://localhost:7262``` for RESTful APIs and ```http://localhost:7263``` for gRPC APIs, certificate is **NOT REQUIRED**.

### Example II

```dotnetcli
dotnet run . -- --cert-path PATH_TO_CERT --cert-password PASSWD
```

It exposes ```http://localhost:7262``` and ```https://localhost:7263```, certificate is **REQUIRED**.

### Example III

```dotnetcli
dotnet run  -- --redirect-to-https
```

It exposes ```https://localhost:7262``` and ```https://localhost:7263```, certificate is **REQUIRED**.

## How to run as a docker container

### Release image

Build docker image by dockerfile for development:

```docker
docker build -t shootingstar.azurecr.io/grpc_service_demo:debug -f ./Dockerfile ../../
```

Build docker image by dockerfile for release:

```docker
docker build -t shootingstar.azurecr.io/grpc_service_demo:latest -f ./Dockerfile ../../
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
