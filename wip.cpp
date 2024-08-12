// #ifndef LINUX_BUILD
// #include "nvs_flash.h"

// extern void app_wifi(void);
// extern void app_websocket(void);

// extern "C" void app_main(void) {
//   esp_err_t ret = nvs_flash_init();
//   if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
//       ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
//     ESP_ERROR_CHECK(nvs_flash_erase());
//     ret = nvs_flash_init();
//   }
//   ESP_ERROR_CHECK(ret);

//   app_wifi();
//   app_websocket();
// }
// #else
// wss://embeded-app-4da61xdw.livekit.cloud
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <time.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#define PORT 443
#define BUFFER_SIZE 1500

static const char b64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static const int mod_table[] = {0, 2, 1};

// Function to encode a byte array to a base64 string
char *base64Encode(const unsigned char *data, size_t input_length, size_t *output_length)
{
  *output_length = 4 * ((input_length + 2) / 3);
  char *encoded_data = (char *)malloc(*output_length + 1);
  if (encoded_data == NULL)
    return NULL;

  for (size_t i = 0, j = 0; i < input_length;)
  {
    uint32_t octet_a = i < input_length ? data[i++] : 0;
    uint32_t octet_b = i < input_length ? data[i++] : 0;
    uint32_t octet_c = i < input_length ? data[i++] : 0;

    uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

    encoded_data[j++] = b64_chars[(triple >> 3 * 6) & 0x3F];
    encoded_data[j++] = b64_chars[(triple >> 2 * 6) & 0x3F];
    encoded_data[j++] = b64_chars[(triple >> 1 * 6) & 0x3F];
    encoded_data[j++] = b64_chars[(triple >> 0 * 6) & 0x3F];
  }

  for (size_t i = 0; i < mod_table[input_length % 3]; ++i)
    encoded_data[*output_length - 1 - i] = '=';

  encoded_data[*output_length] = '\0';
  return encoded_data;
}

// Function to generate a random 16-byte key and encode it as a base64 string
char *generateChallengeKey()
{
  unsigned char bytes[16];
  size_t output_length;

  // Seed the random number generator
  srand((unsigned int)time(NULL));

  // Generate 16 random bytes
  for (int i = 0; i < 16; ++i)
  {
    bytes[i] = rand() % 256;
  }

  // Encode the random bytes to a base64 string
  return base64Encode(bytes, 16, &output_length);
}

int main()
{

  SSL_library_init();
  SSL_load_error_strings();
  OpenSSL_add_all_algorithms();
  SSL_CTX *ctx = SSL_CTX_new(SSLv23_client_method());
  SSL *ssl;

  if (ctx == NULL)
  {
    ERR_print_errors_fp(stderr);
    return 1;
  }

  ssl = SSL_new(ctx);
  if (!ssl)
  {
    perror("Error Creating SSL");
    exit(EXIT_FAILURE);
  }

  char *key = generateChallengeKey();
  if (key)
  {
    printf("Generated key: %s\n", key);
    // free(key); // Remember to free the allocated memory
  }
  else
  {
    fprintf(stderr, "Error generating key.\n");
  }

  const char *hostname = "embeded-app-4da61xdw.livekit.cloud";
  const char *path = "/rtc";
  const char *token = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJleHAiOjE3MjM0MTgwNDAsImlzcyI6IkFQSVRUVXpMY2hhN1JMeiIsIm5iZiI6MTcyMzQxNDQ0MCwic3ViIjoiaWRlbnRpdHkiLCJ2aWRlbyI6eyJyb29tIjoibXktcm9vbSIsInJvb21Kb2luIjp0cnVlfX0._S2u1VpDoxk8xWt56kAhL7b5g9Jb2FEz4l57u5oEgKs";

  int sockfd;
  struct sockaddr_in server_addr;
  struct hostent *server;
  char request[BUFFER_SIZE];
  char response[BUFFER_SIZE];

  // Create socket
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0)
  {
    perror("Error opening socket");
    exit(EXIT_FAILURE);
  }

  // Get server IP address
  server = gethostbyname(hostname);
  if (server == NULL)
  {
    fprintf(stderr, "No such host\n");
    exit(EXIT_FAILURE);
  }

  // Set up server address struct
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(PORT);
  memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);

  // Connect to the server
  int res;
  if ((res = connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr))) < 0)
  {
    perror("Error connecting");
    exit(EXIT_FAILURE);
  }

  SSL_set_fd(ssl, sockfd);
  if (SSL_connect(ssl) <= 0)
  {
    ERR_print_errors_fp(stderr);
    exit(EXIT_FAILURE);
  }

  // Create HTTP GET request
  snprintf(request, sizeof(request),
           "GET %s HTTP/1.1\r\n"
           "Host: %s\r\n"
           "Upgrade: websocket\r\n"
           "Connection: Upgrade\r\n"
           "Sec-WebSocket-Key: %s\r\n"
           "Sec-WebSocket-Version: 13\r\n"
           "Authorization: Bearer %s\r\n"
           "Connection: close\r\n\r\n",
           path, hostname, key, token);

  if (SSL_write(ssl, request, strlen(request)) <= 0)
  {
    ERR_print_errors_fp(stderr);
    exit(EXIT_FAILURE);
  }

  // Read the response
  int n;
  while ((n = SSL_read(ssl, response, sizeof(response) - 1)) > 0)
  {
    response[n] = '\0';
    printf("%s\n", response);
  }

  printf("we got fucked\n");

  if (n < 0)
  {
    perror("Error reading from socket");
  }

  SSL_shutdown(ssl);
  SSL_free(ssl);
  close(sockfd);
  SSL_CTX_free(ctx);
}
// #endif
