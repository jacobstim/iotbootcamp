#r "Newtonsoft.Json"
//#r "Microsoft.Azure.Services"

using System;
using System.Data.SqlClient;
using System.Configuration;
using System.Net;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Text;
using System.Xml;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;
//using Microsoft.Azure.Services.AppAuthentication;

public static async void Run(string myIoTHubMessage, ILogger log)
{
    log.LogInformation($"--- START ---"); 

    // Connection string
    string connectionString = "Server=tcp:<server>.database.windows.net,1433;Initial Catalog=bootcamp;Persist Security Info=False;User ID=<userid>;Password=<password>;MultipleActiveResultSets=False;Encrypt=True;TrustServerCertificate=False;Connection Timeout=30;";
    //string connectionString = "Data Source=iotbootcamp.database.windows.net;Initial Catalog=bootcamp;";

    // Parse JSON input
    log.LogInformation("Parsing JSON: {myIoTHubMessage}", myIoTHubMessage);
    dynamic msg = JObject.Parse(myIoTHubMessage);
    // Convert Unix Epoch time to something SQL will understand
    long timestamp = Convert.ToInt64(msg.MeasurementTime);
    DateTime measurementTime = new DateTime(1970, 1, 1, 0, 0, 0, DateTimeKind.Utc).AddSeconds(timestamp);

    // Get the Managed Service Identity tokens
    string msiEndpoint = Environment.GetEnvironmentVariable("MSI_ENDPOINT");
    string msiSecret = Environment.GetEnvironmentVariable("MSI_SECRET");
    string token = await GetToken("https://database.windows.net", "2017-09-01", msiEndpoint, msiSecret, log);

    // New way of getting a token using the ServiceTokenProvider()
    //var azureServiceTokenProvider = new AzureServiceTokenProvider();
    //string token = await azureServiceTokenProvider.GetAccessTokenAsync("https://database.windows.net");

    log.LogInformation("Got token: {token}", token);

    // Create a new SQL Connection string using the token & open the connection
    using (SqlConnection conn = new SqlConnection(connectionString)) { 
        log.LogInformation("Opening SQL Connection...");
        conn.AccessToken = token;
        try {
            conn.Open();
            log.LogInformation("SQL Connection open...");

            // Execute the following statement
            var sQuery = string.Format(@"INSERT INTO dbo.telemetry
                                (device_id, [time], temperature, humidity, pressure, gas, altitude) 
                                VALUES ('{0}', '{1}', {2:F2}, {3:F2}, {4:F2}, {5:F2}, {6:F2})",
                                msg.DeviceId, measurementTime.ToString("s"), msg.Temperature, msg.Humidity, msg.Pressure, msg.Gas, msg.Altitude);
            log.LogInformation($"SQL query: {sQuery}");
            using (SqlCommand sqlCmd = new SqlCommand(sQuery, conn)) {
                // Execute the command and log the # rows affected.
                var rows = sqlCmd.ExecuteNonQuery();
                log.LogInformation($"SQL: {rows} rows were updated");
            }
            log.LogInformation($"--- DONE ---");
        }
        catch (SqlException ex) {
            // Handle the SQL Exception as you wish
            log.LogError(ex.ToString());
        }
        catch (Exception ex) {
            // Other exceptions
            log.LogError(ex.ToString());
        }
    }

}


//
// Source: https://github.com/StratusOn/MSI-GetToken-FunctionApp/blob/master/GetToken/run.csx
//
// Returns a JSON string of the form (see Token class definition):
// {"access_token":"eyJ0...s1DZw","expires_on":"12/12/2017 10:20:00 AM +00:00","resource":"https://management.azure.com","token_type":"Bearer"}
// Bearer tokens returned are typically valid for only 1 hour.
public static async Task<string> GetToken(string resource, string apiversion, string msiEndpoint, string msiSecret, ILogger log)
{
    string msiUrl = $"{msiEndpoint}?resource={resource}&api-version={apiversion}";
    //log.LogInformation($"MSI Endpoint={msiEndpoint}");
    //log.Info($"MSI secret={msiSecret}");
    //log.LogInformation($"MSI Url={msiUrl}");

    var headers = new Dictionary<string, string>();
    headers.Add("Secret", msiSecret);
    var tokenPayload = await InvokeRestMethodAsync(msiUrl, log, HttpMethod.Get, null, null, null, headers);
    //log.LogInformation($"Token Payload={tokenPayload}");

    return tokenPayload;
}

public static async Task<string> InvokeRestMethodAsync(string url, ILogger log, HttpMethod httpMethod, string body = null, string authorizationToken = null, string authorizationScheme = "Bearer", IDictionary<string, string> headers = null)
{
    HttpClient client = new HttpClient();
    if (!string.IsNullOrWhiteSpace(authorizationToken))
    {
        client.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue(authorizationScheme, authorizationToken);
        log.LogInformation($"Authorization: {client.DefaultRequestHeaders.Authorization.Parameter}");
    }
    
    HttpRequestMessage request = new HttpRequestMessage(httpMethod, url);
    if (headers != null && headers.Count > 0)
    {
        foreach (var header in headers)
        {
            request.Headers.Add(header.Key, header.Value);
        }
    }

    if (!string.IsNullOrWhiteSpace(body))
    {
        request.Content = new StringContent(body, Encoding.UTF8, "application/json");
    }

    HttpResponseMessage response = await client.SendAsync(request);
    if (response.IsSuccessStatusCode)
    {
        return await response.Content.ReadAsStringAsync();
    }

    string statusCodeName = response.StatusCode.ToString();
    int statusCodeValue = (int)response.StatusCode;
    string content = await response.Content.ReadAsStringAsync();
    log.LogInformation($"Status Code: {statusCodeName} ({statusCodeValue}). Body: {content}");

    throw new Exception($"Status Code: {statusCodeName} ({statusCodeValue}). Body: {content}");
}

public class Token
{
    public string access_token { get; set; }
    public DateTime expires_on { get; set; }
    public string resource { get; set; }
    public string token_type { get; set; }
}

