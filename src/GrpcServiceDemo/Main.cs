using GrpcDemo.Services;
using Microsoft.AspNetCore.Authentication.Certificate;
using System.Security.Claims;

using ClientCertificateMode = Microsoft.AspNetCore.Server.Kestrel.Https.ClientCertificateMode;
using HttpProtocols = Microsoft.AspNetCore.Server.Kestrel.Core.HttpProtocols;
using HttpStatusCode = System.Net.HttpStatusCode;
using X509Certificate2 = System.Security.Cryptography.X509Certificates.X509Certificate2;

class Program
{
    static void Main()
    {
        Console.WriteLine("#### CONFIGURATIONS ############################################################");
        var args = new ArgumentParser();
        args.Show();
        if (!args.Validate())
        {
            Console.WriteLine("Error: validate arguments failed.");
            return;
        }

        Console.WriteLine("\n#### START SERVICE #############################################################");
        var builder = WebApplication.CreateBuilder(Environment.GetCommandLineArgs());
        if (!ConfigureWebAppBuilder(builder, args))
        {
            Console.WriteLine("Error: failed to configure the web application builder.");
            return;
        }

        var app = builder.Build();
        if (!BuildWebApp(app, args))
        {
            Console.WriteLine("Error: failed to build the web application.");
            return;
        }
        app.Run();
    }

    static private bool ConfigureWebAppBuilder(WebApplicationBuilder builder, ArgumentParser args)
    {
        builder.WebHost.ConfigureKestrel(options =>
        {
            options.ConfigureHttpsDefaults(o =>
            {
                // Setup the default certificate before option.UseHttps().
                o.ServerCertificate = new X509Certificate2(args.CertPath, args.CertPassword);
                o.ClientCertificateMode = ClientCertificateMode.RequireCertificate;
            });

            // For some RESTful APIs of plain text.
            options.ListenAnyIP(
                7262, o =>
                {
                    o.Protocols = HttpProtocols.Http1;
                });

            // For gRPC APIs.
            options.ListenAnyIP(
                7263, o =>
                {
                    o.Protocols = HttpProtocols.Http2;
                    if (!args.WithHttp || args.RedirectToHttps)
                    {
                        o.UseHttps();
                    }
                });
        });

        if (args.RedirectToHttps)
        {
            builder.Services.AddHttpsRedirection(options =>
            {
                options.RedirectStatusCode = (int)HttpStatusCode.TemporaryRedirect;
                options.HttpsPort = 7263;
            });
        }

        if (!args.WithHttp || args.RedirectToHttps)
        {
            builder.Services.AddAuthentication(CertificateAuthenticationDefaults.AuthenticationScheme)
                .AddCertificate(options =>
                {
                    options.AllowedCertificateTypes = CertificateTypes.All;
                    options.Events = new CertificateAuthenticationEvents
                    {
                        OnCertificateValidated = context =>
                        {
                            var claims = new[]
                            {
                                new Claim(
                                    ClaimTypes.Name,
                                    context.ClientCertificate.Subject,
                                    ClaimValueTypes.String,
                                    context.Options.ClaimsIssuer)
                            };

                            Console.WriteLine($"Client certificate thumbprint: {context.ClientCertificate.Thumbprint}");
                            Console.WriteLine($"Client certificate subject: {context.ClientCertificate.Subject}");

                            context.Principal = new ClaimsPrincipal(new ClaimsIdentity(claims, context.Scheme.Name));
                            context.Success();

                            return Task.CompletedTask;
                        },
                        OnAuthenticationFailed = context =>
                        {
                            context.NoResult();
                            context.Response.StatusCode = 403;
                            context.Response.ContentType = "text/plain";
                            context.Response.WriteAsync(context.Exception.ToString()).Wait();
                            return Task.CompletedTask;
                        },
                    };
                })
                .AddCertificateCache();
        }

        builder.Services.AddGrpc();

        return true;
    }

    static private bool BuildWebApp(Microsoft.AspNetCore.Builder.WebApplication app, ArgumentParser args)
    {
        if (!args.WithHttp || args.RedirectToHttps)
        {
            app.UseAuthentication();
        }

        if (args.RedirectToHttps)
        {
            app.UseHttpsRedirection();
        }

        // Add a simple RESTful API for demo.
        app.MapGet("/", () => "Hello World! The service is running.");

        // Add gRPC APIs.
        app.MapGrpcService<GreeterService>();

        return true;
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
            }

            if (argv == "--with-http")
            {
                WithHttp = true;
            }
            else if (argv == "--redirect-to-https")
            {
                RedirectToHttps = true;
            }
            else if (argv == "--cert-path")
            {
                argc--;
                if (argc == 0)
                {
                    throw new Exception("Specify certificate path after --cert-path.");
                }

                CertPath = arguments[arguments.Length - argc];
            }
            else if (argv == "--cert-password")
            {
                argc--;
                if (argc == 0)
                {
                    throw new Exception("Specify certificate password after --cert-password.");
                }

                CertPassword = arguments[arguments.Length - argc];
            }
            else
            {
                throw new Exception("Unknown argument: " + argv);
            }

            argc--;
        }
    }

    public bool WithHttp { get; } = false;

    public bool RedirectToHttps { get; } = false;

    public string CertPath { get; } = "NOT_SET";

    public string CertPassword { get; } = "";

    public bool Validate()
    {
        if (WithHttp && RedirectToHttps)
        {
            Console.WriteLine("With HTTP and redirect to HTTPS cannot be set at the same time.");
            return false;
        }

        if (!WithHttp || RedirectToHttps)
        {
            if (!System.IO.File.Exists(CertPath))
            {
                Console.WriteLine("Certificate file does not exist.");
                return false;
            }

            if (String.IsNullOrEmpty(CertPassword))
            {
                Console.WriteLine("Certificate password is empty.");
                return false;
            }
        }

        return true;
    }

    public void Show()
    {
        Console.WriteLine("With HTTP         : " + WithHttp);
        Console.WriteLine("Redirect to HTTPS : " + RedirectToHttps);
        Console.WriteLine("Cert path         : " + CertPath);
        Console.WriteLine("Cert password     : " + (string.IsNullOrEmpty(CertPassword) ? "N/A" : "******"));
        Console.WriteLine("");
    }
}
