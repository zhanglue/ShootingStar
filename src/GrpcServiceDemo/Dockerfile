FROM mcr.microsoft.com/dotnet/sdk:9.0 AS build-env
WORKDIR /ShootingStar

# Copy everything
COPY ./ ./
WORKDIR /ShootingStar/src/GrpcServiceDemo

# Restore as distinct layers
RUN dotnet restore
# Build and publish a release
RUN dotnet publish -c Release --self-contained -r linux-x64 -o /compile_results

# Build runtime image
FROM mcr.microsoft.com/dotnet/runtime-deps:9.0-bookworm-slim
WORKDIR /GrpcServiceDemo
COPY --from=build-env /compile_results/* /GrpcServiceDemo/
ENV DOTNET_EnableDiagnostics=0
ENTRYPOINT ["./GrpcServiceDemo"]
