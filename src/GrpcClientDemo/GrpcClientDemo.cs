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

        string remoteAddress = String.Format("{0}://{1}:{2}", args.IsUsingUntrustedCertificate ? "http" : "https", args.Ip, args.Port.ToString());
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

            if (argv == "--untrusted-ca")
            {
                IsUsingUntrustedCertificate = true;
            }
            else if (argv == "--ip")
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

            argc--;
        }
    }

    public string Ip { get; } = "localhost";

    public int Port { get; } = 7263;

    public bool IsUsingUntrustedCertificate { get; } = false;

    public void Show()
    {
        Console.WriteLine("IP           : " + Ip);
        Console.WriteLine("Port         : " + Port);
        Console.WriteLine("Untrusted CA : " + IsUsingUntrustedCertificate);
    }
}
