using Grpc.Net.Client;
using GrpcClientDemo;

var channel = GrpcChannel.ForAddress("https://localhost:7263");
var client = new Greeter.GreeterClient(channel);
var response = await client.SayHelloAsync(new HelloRequest { Name = "World" });

Console.WriteLine("Greeting: " + response.Message);
