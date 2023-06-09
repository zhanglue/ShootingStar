using Grpc.Core;

namespace GrpcDemo.Services;

public class GreeterService : Greeter.GreeterBase
{
    private readonly ILogger<GreeterService> _logger;
    public GreeterService(ILogger<GreeterService> logger)
    {
        _logger = logger;
    }

    public override Task<HelloReply> SayHello(HelloRequest request, ServerCallContext context)
    {
        _logger.LogInformation($"Client authenticated: {context.AuthContext.IsPeerAuthenticated}");
        if (context.AuthContext.IsPeerAuthenticated)
        {
            _logger.LogInformation($"Auth property name: {context.AuthContext.PeerIdentityPropertyName}");
            _logger.LogInformation($"Auth property value: {context.AuthContext.Properties.FirstOrDefault()?.Value}");
        }

        return Task.FromResult(new HelloReply
        {
            Message = "Hello " + request.Name
        });
    }
}
