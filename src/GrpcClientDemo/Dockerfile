FROM mcr.microsoft.com/dotnet/sdk:7.0 AS build-env
WORKDIR /ShootingStar

# Copy everything
COPY ./ ./
WORKDIR /ShootingStar/src/GrpcClientDemo

# Restore as distinct layers
RUN dotnet restore
# Build and publish a release
RUN dotnet publish -c Release --self-contained -r linux-x64 -o /compile_results

# Build runtime image
FROM mcr.microsoft.com/dotnet/nightly/runtime-deps:7.0-jammy-chiseled
WORKDIR /GrpcClientDemo
COPY --from=build-env /compile_results/* /GrpcClientDemo/
ENV DOTNET_EnableDiagnostics=0
ENTRYPOINT ["./GrpcClientDemo"]
