if(CONFIG_APP_MQTT_CLIENT_MODULE)
target_sources(app PRIVATE app_mqtt_client.c)
# Make our ca-cert.pem discoverable via the __has_include("ca-cert.pem")
# fallback in mqtt_helper's default MQTT_HELPER_CERTIFICATES_FILE template
# (nrf/subsys/net/lib/mqtt_helper/cert/mqtt-certs.h) on NCS v2.6.4, which has
# no MQTT_HELPER_CERTIFICATES_FOLDER option.
zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/cert)
endif()
