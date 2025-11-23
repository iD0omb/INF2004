#ifndef MQTT_OPS_H
#define MQTT_OPS_H

#include <stdbool.h>

// Initialize the MQTT client and start DNS resolution
void mqtt_init(void);

// Publish a JSON report to the configured topic
// Returns true if the publish request was queued successfully
bool mqtt_publish_report(const char *json_data);

// Check current connection status
bool mqtt_is_connected(void);

#endif // MQTT_OPS_H