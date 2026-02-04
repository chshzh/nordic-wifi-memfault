/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "mqtt_client.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <net/mqtt_helper.h>
#include <hw_id.h>
#include <memfault/metrics/metrics.h>

#if defined(CONFIG_POSIX_API)
#include <zephyr/posix/netdb.h>
#include <zephyr/posix/sys/socket.h>
#endif

LOG_MODULE_REGISTER(mqtt_client, CONFIG_MQTT_CLIENT_LOG_LEVEL);

#define DNS_CHECK_INTERVAL_SEC 10

/* MQTT client states - prefixed to avoid conflict with mqtt_helper.h */
enum app_mqtt_client_state {
	APP_MQTT_STATE_DISCONNECTED,
	APP_MQTT_STATE_CONNECTING,
	APP_MQTT_STATE_CONNECTED,
};

/* State variables */
static enum app_mqtt_client_state current_state = APP_MQTT_STATE_DISCONNECTED;
static bool mqtt_client_running = false;
static bool network_ready = false;
static uint32_t message_count;
static uint32_t mqtt_echo_total;
static uint32_t mqtt_echo_failures;
static K_SEM_DEFINE(mqtt_thread_sem, 0, 1);

/* Client ID and topic buffers */
static char client_id[CONFIG_MQTT_CLIENT_ID_BUFFER_SIZE];
static char pub_topic[CONFIG_MQTT_CLIENT_ID_BUFFER_SIZE +
		      sizeof(CONFIG_MQTT_CLIENT_PUBLISH_TOPIC)];

/* MQTT helper callbacks */
static void on_mqtt_connack(enum mqtt_conn_return_code return_code,
			    bool session_present)
{
	ARG_UNUSED(session_present);

	if (return_code != MQTT_CONNECTION_ACCEPTED) {
		LOG_ERR("MQTT broker rejected connection, return code: %d",
			return_code);
		current_state = APP_MQTT_STATE_DISCONNECTED;
		return;
	}

	LOG_INF("Connected to MQTT broker");
	LOG_INF("Hostname: %s", CONFIG_MQTT_CLIENT_BROKER_HOSTNAME);
	LOG_INF("Client ID: %s", client_id);
	LOG_INF("Port: %d", CONFIG_MQTT_HELPER_PORT);
	LOG_INF("TLS: Yes");

	current_state = APP_MQTT_STATE_CONNECTED;

	/* Subscribe to the publish topic for echo test */
	if (pub_topic[0] != '\0') {
		struct mqtt_topic sub_topic = {
			.topic.utf8 = pub_topic,
			.topic.size = strlen(pub_topic),
			.qos = MQTT_QOS_1_AT_LEAST_ONCE,
		};
		struct mqtt_subscription_list sub_list = {
			.list = &sub_topic,
			.list_count = 1,
			.message_id = mqtt_helper_msg_id_get(),
		};
		int err = mqtt_helper_subscribe(&sub_list);
		if (err) {
			LOG_WRN("Failed to subscribe to topic: %d", err);
		} else {
			LOG_INF("Subscribing to topic: %s", pub_topic);
		}
	}
}

static void on_mqtt_disconnect(int result)
{
	LOG_INF("Disconnected from MQTT broker, result: %d", result);
	current_state = APP_MQTT_STATE_DISCONNECTED;

	/* If network is still ready, this was an unexpected disconnect.
	 * Could be transient network issue, NAT timeout, or broker disconnect.
	 */
	if (network_ready) {
		if (result == -128) {
			/* Socket closed - common during network startup */
			LOG_DBG("Connection closed (network stabilizing), will "
				"retry");
		} else {
			LOG_WRN("Unexpected disconnect (code %d), will attempt "
				"reconnection",
				result);
		}
	}
}

static void on_mqtt_publish(struct mqtt_helper_buf topic,
			    struct mqtt_helper_buf payload)
{
	LOG_INF("Received payload: %.*s on topic: %.*s", payload.size,
		payload.ptr, topic.size, topic.ptr);

	/* Update MQTT echo metrics - message received back successfully */
	mqtt_echo_total++;
	MEMFAULT_METRIC_SET_UNSIGNED(mqtt_echo_total_count, mqtt_echo_total);
	LOG_INF("MQTT Echo Test Metrics - Total: %u, Failures: %u",
		mqtt_echo_total, mqtt_echo_failures);
}

static void on_mqtt_suback(uint16_t message_id, int result)
{
	if (result == 0) {
		LOG_INF("Subscription successful, message_id: %d", message_id);
	} else {
		LOG_ERR("Subscription failed, error: %d", result);
	}
}

static int setup_topics(void)
{
	int len;

	len = snprintk(pub_topic, sizeof(pub_topic), "Memfault/%s/%s",
		       client_id, CONFIG_MQTT_CLIENT_PUBLISH_TOPIC);
	if ((len < 0) || (len >= sizeof(pub_topic))) {
		LOG_ERR("Publish topic buffer too small");
		return -EMSGSIZE;
	}

	LOG_DBG("Configured publish topic: %s", pub_topic);
	return 0;
}

static bool check_dns_ready(const char *hostname)
{
	int err;
	struct addrinfo *res = NULL;
	struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
	};

	err = getaddrinfo(hostname, "8883", &hints, &res);
	if (res) {
		freeaddrinfo(res);
	}

	return (err == 0);
}

static int app_mqtt_connect(void)
{
	int err;

	if (current_state == APP_MQTT_STATE_CONNECTED) {
		LOG_DBG("Already connected to MQTT broker");
		return 0;
	}

	if (current_state == APP_MQTT_STATE_CONNECTING) {
		LOG_DBG("Already connecting to MQTT broker, waiting...");
		/* Return error to keep the retry loop waiting */
		return -EINPROGRESS;
	}

	current_state = APP_MQTT_STATE_CONNECTING;

	/* Get client ID based on hardware ID (MAC address) */
	err = hw_id_get(client_id, sizeof(client_id));
	if (err) {
		LOG_ERR("Failed to get hardware ID: %d", err);
		current_state = APP_MQTT_STATE_DISCONNECTED;
		return err;
	}

	err = setup_topics();
	if (err) {
		LOG_ERR("Failed to setup topics: %d", err);
		current_state = APP_MQTT_STATE_DISCONNECTED;
		return err;
	}

	struct mqtt_helper_conn_params conn_params = {
		.hostname.ptr = CONFIG_MQTT_CLIENT_BROKER_HOSTNAME,
		.hostname.size = strlen(CONFIG_MQTT_CLIENT_BROKER_HOSTNAME),
		.device_id.ptr = client_id,
		.device_id.size = strlen(client_id),
	};

	LOG_INF("Connecting to MQTT broker: %s",
		CONFIG_MQTT_CLIENT_BROKER_HOSTNAME);

	err = mqtt_helper_connect(&conn_params);
	if (err) {
		LOG_ERR("Failed to connect to MQTT broker: %d", err);
		current_state = APP_MQTT_STATE_DISCONNECTED;
		return err;
	}

	return 0;
}

static int mqtt_publish_message(void)
{
	int err;
	char payload[128];

	if (!network_ready) {
		LOG_WRN("Network not ready, skipping publish");
		return -ENETDOWN;
	}

	if (current_state != APP_MQTT_STATE_CONNECTED) {
		LOG_WRN("Not connected to MQTT broker, skipping publish");
		return -ENOTCONN;
	}

	message_count++;
	snprintk(payload, sizeof(payload), "%u", message_count);

	struct mqtt_publish_param param = {
		.message.payload.data = payload,
		.message.payload.len = strlen(payload),
		.message.topic.qos = MQTT_QOS_1_AT_LEAST_ONCE,
		.message_id = mqtt_helper_msg_id_get(),
		.message.topic.topic.utf8 = pub_topic,
		.message.topic.topic.size = strlen(pub_topic),
	};

	err = mqtt_helper_publish(&param);
	if (err) {
		LOG_WRN("Failed to publish message: %d", err);
		/* Update failure metric */
		mqtt_echo_failures++;
		MEMFAULT_METRIC_SET_UNSIGNED(mqtt_echo_fail_count,
					     mqtt_echo_failures);
		return err;
	}

	LOG_INF("Published message: \"%s\" on topic: \"%s\"", payload,
		pub_topic);
	return 0;
}

static void mqtt_client_thread(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	int err;

	LOG_INF("MQTT client thread started");

	/* Configure MQTT helper callbacks */
	struct mqtt_helper_cfg cfg = {
		.cb =
			{
				.on_connack = on_mqtt_connack,
				.on_disconnect = on_mqtt_disconnect,
				.on_publish = on_mqtt_publish,
				.on_suback = on_mqtt_suback,
			},
	};

	err = mqtt_helper_init(&cfg);
	if (err) {
		LOG_ERR("Failed to initialize MQTT helper: %d", err);
		return;
	}

	while (mqtt_client_running) {
		/* Wait for network connection */
		k_sem_take(&mqtt_thread_sem, K_FOREVER);

		if (!mqtt_client_running) {
			break;
		}

		if (!network_ready) {
			LOG_WRN("Network not ready after semaphore signal, "
				"skipping");
			continue;
		}

		LOG_INF("Network ready, waiting for DNS resolver");

		/* Wait for DNS to be ready - check every 10 seconds, timeout
		 * after 5 minutes */
		int dns_wait_time = 0;
		const int dns_timeout = 300; /* 5 minutes */
		while (network_ready &&
		       !check_dns_ready(CONFIG_MQTT_CLIENT_BROKER_HOSTNAME)) {
			if (dns_wait_time >= dns_timeout) {
				LOG_ERR("DNS timeout after %d seconds for %s, "
					"continuing anyway",
					dns_timeout,
					CONFIG_MQTT_CLIENT_BROKER_HOSTNAME);
				break;
			}
			LOG_INF("DNS not ready for %s, checking again in %d "
				"seconds",
				CONFIG_MQTT_CLIENT_BROKER_HOSTNAME,
				DNS_CHECK_INTERVAL_SEC);
			k_sleep(K_SECONDS(DNS_CHECK_INTERVAL_SEC));
			dns_wait_time += DNS_CHECK_INTERVAL_SEC;
		}

		/* Verify network is still ready after DNS checks */
		if (!network_ready) {
			LOG_WRN("Network disconnected during DNS wait, "
				"aborting");
			continue;
		}

		if (dns_wait_time < dns_timeout) {
			LOG_INF("DNS ready for %s after %d seconds, starting "
				"MQTT operations",
				CONFIG_MQTT_CLIENT_BROKER_HOSTNAME,
				dns_wait_time);
		} else {
			LOG_WRN("Starting MQTT operations without DNS "
				"confirmation");
		}

		/* Main MQTT operation loop - handles connect, publish, and
		 * reconnect */
		while (mqtt_client_running && network_ready) {
			int retry_count = 0;
			const int max_quick_retries = 3;

			/* Try to connect to MQTT broker if not connected */
			while (mqtt_client_running && network_ready &&
			       current_state != APP_MQTT_STATE_CONNECTED) {
				err = app_mqtt_connect();
				if (err == -EINPROGRESS) {
					/* Connection in progress, wait briefly
					 * and check again */
					k_sleep(K_MSEC(500));
				} else if (err) {
					retry_count++;
					if (retry_count <= max_quick_retries) {
						/* Quick retries for transient
						 * network startup issues */
						LOG_INF("Initial connection "
							"attempt %d/%d failed, "
							"retrying in 5 seconds",
							retry_count,
							max_quick_retries);
						k_sleep(K_SECONDS(5));
					} else {
						/* Longer backoff after initial
						 * retries */
						LOG_WRN("MQTT connection "
							"failed after %d "
							"attempts, "
							"retrying in %d "
							"seconds",
							retry_count,
							CONFIG_MQTT_CLIENT_RECONNECT_TIMEOUT_SEC);
						k_sleep(K_SECONDS(
							CONFIG_MQTT_CLIENT_RECONNECT_TIMEOUT_SEC));
					}
				} else {
					/* Connection initiated successfully,
					 * reset retry counter */
					retry_count = 0;
					/* Wait for callback to update state */
					k_sleep(K_MSEC(500));
				}
			}

			/* Publish messages periodically while connected */
			while (mqtt_client_running && network_ready &&
			       current_state == APP_MQTT_STATE_CONNECTED) {
				mqtt_publish_message();
				k_sleep(K_SECONDS(
					CONFIG_MQTT_CLIENT_PUBLISH_INTERVAL_SEC));
			}

			/* If we get here and network is still ready, broker
			 * disconnected us. Wait briefly then reconnect.
			 */
			if (mqtt_client_running && network_ready &&
			    current_state == APP_MQTT_STATE_DISCONNECTED) {
				LOG_INF("Broker connection lost, reconnecting "
					"in %d seconds",
					CONFIG_MQTT_CLIENT_RECONNECT_TIMEOUT_SEC);
				k_sleep(K_SECONDS(
					CONFIG_MQTT_CLIENT_RECONNECT_TIMEOUT_SEC));
			}
		}

		LOG_INF("Network disconnected or client stopped");
	}

	LOG_INF("MQTT client thread exiting");
}

K_THREAD_DEFINE(mqtt_client_tid, CONFIG_MQTT_CLIENT_STACK_SIZE,
		mqtt_client_thread, NULL, NULL, NULL,
		CONFIG_MQTT_CLIENT_THREAD_PRIORITY, 0, 0);

int app_mqtt_client_init(void)
{
	LOG_INF("MQTT client initialized");
	mqtt_client_running = true;
	return 0;
}

void app_mqtt_client_notify_connected(void)
{
	if (mqtt_client_running && !network_ready) {
		LOG_INF("Network connected, notifying MQTT client");
		network_ready = true;
		k_sem_give(&mqtt_thread_sem);
	} else if (network_ready) {
		LOG_DBG("Network already marked as ready, skipping duplicate "
			"notification");
	}
}

void app_mqtt_client_notify_disconnected(void)
{
	LOG_INF("Network disconnected, stopping MQTT client");
	network_ready = false;

	/* Disconnect from broker if connected */
	if (current_state == APP_MQTT_STATE_CONNECTED) {
		mqtt_helper_disconnect();
		current_state = APP_MQTT_STATE_DISCONNECTED;
	}
}

int app_mqtt_client_publish(const char *payload)
{
	int err;

	if (current_state != APP_MQTT_STATE_CONNECTED) {
		LOG_WRN("Not connected to MQTT broker");
		return -ENOTCONN;
	}

	if (!payload) {
		return -EINVAL;
	}

	struct mqtt_publish_param param = {
		.message.payload.data = (uint8_t *)payload,
		.message.payload.len = strlen(payload),
		.message.topic.qos = MQTT_QOS_1_AT_LEAST_ONCE,
		.message_id = mqtt_helper_msg_id_get(),
		.message.topic.topic.utf8 = pub_topic,
		.message.topic.topic.size = strlen(pub_topic),
	};

	err = mqtt_helper_publish(&param);
	if (err) {
		LOG_WRN("Failed to publish message: %d", err);
		return err;
	}

	LOG_INF("Published message: \"%s\" on topic: \"%s\"", payload,
		pub_topic);
	return 0;
}
