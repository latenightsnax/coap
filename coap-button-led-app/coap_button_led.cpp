#include <algorithm>
#include <iostream>

#include <boost/property_tree/json_parser.hpp>

#include <coap.h>
#include <wiringPi.h>


/// CONNECTION SETUP
/// ----------------
///
///
/// LED and Button                          Raspberry Pi 3
///                                        40-pin Pi Wedge
/// ======================================================
/// LED
/// Anode (long leg) LED  ------------------------->  G17
/// Cathode (short leg)  LED ------> 330 Ohm ------>  GND
/// 
/// Button
/// Button first pin   ---------------------------->  G18 (internal pull-down)
/// Button second pin  ---------------------------->  3.3V


/// max. string length
#define MAX_LEN 255

/// GPIO Pin for LED (GPIO 0 corresponds to pin G17 on Pi Wedge connector)
const int led_pin = 0;

/// GPIO Pin for Button (GPIO 1 corresponds to pin G18 on Pi Wedge connector)
const int button_pin = 1;

/// Port number of COAP server
const int port = 5683;

/// State of the LED
static int led_state = 0;


/// GET handler for `/hello` resource
static void
hello_handler(coap_context_t *ctx, struct coap_resource_t *resource, 
              const coap_endpoint_t *local_interface, coap_address_t *peer, 
              coap_pdu_t *request, str *token, coap_pdu_t *response) 
{
  unsigned char buf[3];
  const char* response_data = "Hello World!";
  response->hdr->code = COAP_RESPONSE_CODE(205);
  coap_add_option(response, COAP_OPTION_CONTENT_TYPE, 
    coap_encode_var_bytes(buf, COAP_MEDIATYPE_TEXT_PLAIN), buf);
  coap_add_data(response, strlen(response_data), 
    (unsigned char *)response_data);
}

// GET handler for `/button` resource
static void
button_handler(coap_context_t *ctx, struct coap_resource_t *resource, 
               const coap_endpoint_t *local_interface, coap_address_t *peer, 
               coap_pdu_t *request, str *token, coap_pdu_t *response) {
  int button = digitalRead(button_pin);
  unsigned char buf[MAX_LEN];
  response->hdr->code = COAP_RESPONSE_CODE(205); // content
  coap_add_option(response, COAP_OPTION_CONTENT_TYPE, 
    coap_encode_var_bytes(buf, COAP_MEDIATYPE_TEXT_PLAIN), buf);
  int len = snprintf((char*)buf, 
      std::min(sizeof(buf), response->max_size - response->length),
      "{\"button\": %d}", button);
  coap_add_data(response, len, buf);
}

/// GET hadler for `/led` resource
static void
led_get_handler(coap_context_t *ctx, struct coap_resource_t *resource, 
              const coap_endpoint_t *local_interface, coap_address_t *peer, 
              coap_pdu_t *request, str *token, coap_pdu_t *response) 
{
  unsigned char buf[MAX_LEN];
  response->hdr->code = COAP_RESPONSE_CODE(205); // content
  coap_add_option(response, COAP_OPTION_CONTENT_TYPE, 
    coap_encode_var_bytes(buf, COAP_MEDIATYPE_TEXT_PLAIN), buf);
  int len = snprintf((char*)buf, 
      std::min(sizeof(buf), response->max_size - response->length),
      "{ \"led\": %d}", led_state);
  coap_add_data(response, len, buf);
}

/// POST handler for `/led` resource
static void
led_post_handler(coap_context_t *ctx, struct coap_resource_t *resource, 
              const coap_endpoint_t *local_interface, coap_address_t *peer, 
              coap_pdu_t *request, str *token, coap_pdu_t *response) 
{
  unsigned char *data;
  size_t size; 
  coap_get_data(request, &size, &data); 
  std::string json_string(reinterpret_cast<char*>(data), size);
  std::cout << "request: " << json_string << std::endl; 
  // parse JSON string from request
  boost::property_tree::ptree tree;
  int new_led_state;
  try 
  { 
    std::istringstream strstream(json_string); 
    boost::property_tree::read_json(strstream, tree);
    new_led_state = tree.get<int>("led"); // get 'led' attribute
    response->hdr->code = COAP_RESPONSE_CODE(204); // 2.04 changed
  } 
  catch(const std::exception & e) 
  { 
    std::cerr << "error parsing json: " << e.what() << std::endl;
    response->hdr->code = COAP_RESPONSE_CODE(402); // 4.02 bad operation
    return;
  }

  std::cout << "new led state: " << new_led_state << std::endl;
  led_state = new_led_state;
  digitalWrite(led_pin, led_state);
}

/// Initialize GPIOs for LED and Button
static void initialize_gpios()
{
  wiringPiSetup();
  pinMode(led_pin, OUTPUT);
  pinMode(button_pin, INPUT);
  pullUpDnControl(button_pin, PUD_DOWN); // enable chip-interal pull-down resistor
  digitalWrite(led_pin, led_state);
}


int main(int argc, char* argv[])
{
  initialize_gpios();

  // Prepare the CoAP server socket 
  coap_address_t serv_addr;
  coap_address_init(&serv_addr);
  serv_addr.addr.sin.sin_family      = AF_INET;
  serv_addr.addr.sin.sin_addr.s_addr = INADDR_ANY;
  serv_addr.addr.sin.sin_port        = htons(port); 
	
  coap_context_t* ctx = coap_new_context(&serv_addr);
  if (ctx == nullptr) {
    std::cerr << "unable to create new CoAP context\n"; 
    exit(EXIT_FAILURE);
  }
  std::cout << "CoAP Server listening on port " << port << "...\n";
  
  // create hello resource and register handler for GET requests
  coap_resource_t* hello_resource = coap_resource_init((unsigned char *)"hello", 5, 0);
  coap_register_handler(hello_resource, COAP_REQUEST_GET, hello_handler);
  coap_add_resource(ctx, hello_resource);

  /// create button resource and register handler for GET requests
  coap_resource_t* button_resource = coap_resource_init((unsigned char *)"button", 6, 0);
  coap_register_handler(button_resource, COAP_REQUEST_GET, button_handler);
  coap_add_resource(ctx, button_resource);
  
  /// create led resource and register handler for GET and POST requests
  coap_resource_t* led_resource = coap_resource_init((unsigned char *)"led", 3, 0);
  coap_register_handler(led_resource, COAP_REQUEST_GET, led_get_handler);
  coap_register_handler(led_resource, COAP_REQUEST_POST, led_post_handler);
  coap_add_resource(ctx, led_resource);

  // listen for incoming connections 
  fd_set readfds;    
  while (true) {
    // wait until a client sends a request
    FD_ZERO(&readfds);
    FD_SET(ctx->sockfd, &readfds);
    int result = select(FD_SETSIZE, &readfds, 0, 0, NULL);
    if (result < 0) { // socket error
      exit(EXIT_FAILURE);
    } else if (result > 0 && FD_ISSET(ctx->sockfd, &readfds)) {
      // new datagram received, read datagram -> calls corresponding handler
      coap_read(ctx);       
    } 
  }
  return 0;
}
