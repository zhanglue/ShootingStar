using GrpcDemo.Services;
using System.Net;

class Program
{
    static void Main()
    {
        Console.WriteLine("#### CONFIGURATIONS ############################################################");

        var args = new ArgumentParser();
        args.Show();

        Console.WriteLine("\n#### START SERVICE #############################################################");

        var builder = WebApplication.CreateBuilder(Environment.GetCommandLineArgs());
        builder.WebHost.ConfigureKestrel(options =>
        {
            // For some RESTful APIs of plain text.
            options.ListenAnyIP(
                7262, o =>
                {
                    o.Protocols = Microsoft.AspNetCore.Server.Kestrel.Core.HttpProtocols.Http1;
                    if (!args.WithHttp)
                    {
                        o.UseHttps();
                    }
                });
            // For gRPC APIs.
            options.ListenAnyIP(
                7263, o =>
                {
                    o.Protocols = Microsoft.AspNetCore.Server.Kestrel.Core.HttpProtocols.Http2;
                    if (!args.WithHttp || args.ForceHttps)
                    {
                        o.UseHttps();
                    }
                });
        });

        // The endpoints could be defined in the appsettings.json file, too.
        // Such as following:
        // "Kestrel": {
        //   "Endpoints": {
        //     "ForRESTful": {
        //         "Url": "http://localhost:7262",
        //         "Protocols": "Http1"
        //     },
        //     "ForGRPC": {
        //         "Url": "https://localhost:7263",
        //         "Protocols": "Http2"
        //     }
        //   },
        //  "EndpointDefaults": {
        //     "Protocols": "Http1AndHttp2"
        //   },
        //  "Certificates": {
        //     "Default": {
        //       "Path": "localhost.pfx",
        //       "Password": "123456",
        //       "AllowInvalid": "false"
        //     }
        //   }
        // }
        // 1.   The URL specified the schema (HTTP or HTTPS) and the port number at the same time.
        //      It will conflict if define the endpoints both in the appsettings.json file and the code.
        //      So, we just define the endpoints in the code for more flexible.
        // 2.   As usual:
        // 2.1  It is not necessary to define the endpoints both of protocols Http1 and Http2.
        //      Define the endpoint of Http1 is enough for RESTful APIs, while define the endpoint of Http2 is enough for gRPC APIs.
        //      It is a little bit of weird to define the endpoints both of protocols Http1 and Http2,
        //      while it means that the service will expose both RESTful APIs and gRPC APIs.
        //      The protocols is not required for the endpoints, it will be set to Http1AndHttp2 by EndpointsDefaults.Protocols.
        //      In this case, the HTTPS is force to be enabled, and the HTTP is disabled.
        // 2.2  The certificate is not required for the HTTP endpoints.
        //      It is possible to use various certificates for different HTTPS endpoints by specifying the details in endpoint.
        //      If not specify the certificate for the endpoint, the default certificate will be used.

        builder.Services.AddGrpc();
        if (args.ForceHttps)
        {
            builder.Services.AddHttpsRedirection(options =>
            {
                options.RedirectStatusCode = (int)HttpStatusCode.TemporaryRedirect;
                options.HttpsPort = 7263;
            });
        }

        var app = builder.Build();
        if (args.ForceHttps)
        {
            app.UseHttpsRedirection();
        }
        // Add a simple RESTful API for demo.
        app.MapGet("/", () => "Communication with gRPC endpoints must be made through a gRPC client. To learn how to create a client, visit: https://go.microsoft.com/fwlink/?linkid=2086909");
        // Add gRPC APIs.
        app.MapGrpcService<GreeterService>();

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

            if (argv == "--force-https")
            {
                ForceHttps = true;
            }

            argc--;
        }
    }

    public bool WithHttp { get; } = false;

    public bool ForceHttps { get; } = false;

    public void Show()
    {
        Console.WriteLine("With HTTP        : " + WithHttp);
        Console.WriteLine("Force with HTTPS : " + ForceHttps);
    }
}
