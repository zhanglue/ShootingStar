using GrpcServiceDemo.Services;

class Program
{
    static void Main()
    {
        Console.WriteLine("################################################################################");
        Console.WriteLine("# Configurations:");

        var args = new ArgumentParser();
        args.Show();

        Console.WriteLine("\n################################################################################");
        Console.WriteLine("# Start service:");

        var builder = WebApplication.CreateBuilder(Environment.GetCommandLineArgs());
        builder.Services.AddGrpc();
        if (args.WithHttp)
        {
            builder.WebHost.ConfigureKestrel(options =>
            {
                // Setup a HTTP/2 endpoint without TLS.
                options.ListenAnyIP(7263, o => o.Protocols = Microsoft.AspNetCore.Server.Kestrel.Core.HttpProtocols.Http2);
            });
        }

        var app = builder.Build();
        app.MapGrpcService<GreeterService>();
        app.MapGet("/", () => "Communication with gRPC endpoints must be made through a gRPC client. To learn how to create a client, visit: https://go.microsoft.com/fwlink/?linkid=2086909");

        app.Run();
    }
}

class ArgumentParser
{
    public ArgumentParser()
    {
        string[] arguments = Environment.GetCommandLineArgs();
        int argc = arguments.Length - 1;
        bool skippedFirstDot = false;

        while (argc > 0)
        {
            string argv = arguments[arguments.Length - argc];

            if (!skippedFirstDot && (argv == "." || argv == "./"))
            {
                skippedFirstDot = true;
                argc--;
                continue;
            }

            if (argv == "--with-http")
            {
                WithHttp = true;
            }

            argc--;
        }
    }

    public bool WithHttp { get; } = false;

    public void Show()
    {
        Console.WriteLine("With HTTP : " + WithHttp);
    }
}
