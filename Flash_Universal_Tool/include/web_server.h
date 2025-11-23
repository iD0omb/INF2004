#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "lwip/ip_addr.h"

// Initialize the HTTP server
// We pass the current IP address so the HTML can display it
void http_server_init(const char *ip_address);

#endif // WEB_SERVER_H