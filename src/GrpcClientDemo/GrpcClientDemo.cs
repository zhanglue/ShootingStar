using Grpc.Net.Client;
using GrpcDemo;

class Program
{
    static async Task Main()
    {
        Console.WriteLine("#### CONFIGURATIONS ############################################################");
        var args = new ArgumentParser();
        args.Show();

        Console.WriteLine("\n#### CALL REMOTE ###############################################################");

        string remoteAddress = String.Format("{0}://{1}:{2}", args.WithHttp ? "http" : "https", args.Ip, args.Port.ToString());
        Console.WriteLine("Remote address: " + remoteAddress);

        var httpHandler = new HttpClientHandler();
        if (args.IsUsingUntrustedCertificate)
        {
            httpHandler.ServerCertificateCustomValidationCallback =
                HttpClientHandler.DangerousAcceptAnyServerCertificateValidator;
        }

        var channel = GrpcChannel.ForAddress(remoteAddress, new GrpcChannelOptions { HttpHandler = httpHandler });
        var client = new Greeter.GreeterClient(channel);
        var response = await client.SayHelloAsync(new HelloRequest { Name = "World" });

        Console.WriteLine("Greeting: " + response.Message);
    }
}

class ArgumentParser
{
    private readonly string[] localhosts = {"localhost", "127.0.0.1"};
    private bool isUsingUntrustedCertificate = false;

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

            if (argv == "--ip")
            {
                argc--;
                if (argc == 0)
                {
                    throw new Exception("Specify IP after --ip.");
                }

                Ip = arguments[arguments.Length - argc];
            }
            else if (argv == "--port")
            {
                argc--;
                if (argc == 0)
                {
                    throw new Exception("Specify port after --port.");
                }

                Port = int.Parse(arguments[arguments.Length - argc]);
            }
            else if (argv == "--with-http")
            {
                WithHttp = true;
            }
            else if (argv == "--untrusted-ca")
            {
                isUsingUntrustedCertificate = true;
            }

            argc--;
        }
    }

    public string Ip { get; } = "localhost";

    public int Port { get; } = 7263;

    public bool WithHttp { get; } = false;

    public bool IsUsingUntrustedCertificate
    {
        get => isUsingUntrustedCertificate || localhosts.Contains(Ip);
    }

    public void Show()
    {
        Console.WriteLine("IP           : " + Ip);
        Console.WriteLine("Port         : " + Port);
        Console.WriteLine("With HTTP    : " + WithHttp);
        Console.WriteLine("Untrusted CA : " + IsUsingUntrustedCertificate);
    }
}
