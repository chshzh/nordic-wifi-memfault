/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "app_https_client.h"
#include "../messages.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <stdlib.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/init.h>
#ifdef CONFIG_MEMFAULT
#include <memfault/metrics/metrics.h>
#else
#define MEMFAULT_METRIC_SET_UNSIGNED(...) ((void)0)
#endif

#if defined(CONFIG_POSIX_API)
#include <zephyr/posix/arpa/inet.h>
#include <zephyr/posix/netdb.h>
#include <zephyr/posix/unistd.h>
#include <zephyr/posix/sys/socket.h>
#endif

#if defined(CONFIG_MODEM_KEY_MGMT)
#include <modem/modem_key_mgmt.h>
#endif

LOG_MODULE_REGISTER(app_https_client, CONFIG_APP_HTTPS_CLIENT_LOG_LEVEL);

#define HTTPS_PORT "443"
#define HTTP_HEAD                                                              \
	"HEAD / HTTP/1.1\r\n"                                                  \
	"Host: " CONFIG_APP_HTTPS_HOSTNAME ":" HTTPS_PORT "\r\n"               \
	"Connection: close\r\n\r\n"

#define HTTP_HEAD_LEN (sizeof(HTTP_HEAD) - 1)
#define HTTP_HDR_END  "\r\n\r\n"

#define RECV_BUF_SIZE 2048
#define TLS_SEC_TAG   42

#define HTTPS_REQUEST_INTERVAL_SEC CONFIG_APP_HTTPS_REQUEST_INTERVAL_SEC
#define DNS_CHECK_INTERVAL_SEC     10
static const char send_buf[] = HTTP_HEAD;
static char recv_buf[RECV_BUF_SIZE];
static K_SEM_DEFINE(https_thread_sem, 0, 1);
static bool https_client_running;
static bool network_ready;
static uint32_t https_req_total;    /* Local counter for total requests */
static uint32_t https_req_failures; /* Local counter for failed requests */

/* Certificate for hostname */
static const char cert[] = {
#include "SSLcom-TLS-Root-2022-ECC.pem.inc"

	/* Null terminate certificate if running Mbed TLS on the application
	 * core. Required by TLS credentials API.
	 */
	IF_ENABLED(CONFIG_TLS_CREDENTIALS, (0x00)) };

BUILD_ASSERT(sizeof(cert) < KB(4), "Certificate too large");

/* Provision certificate to modem */
static int cert_provision(void)
{
	int err;

	LOG_INF("Provisioning certificate");

#if CONFIG_MODEM_KEY_MGMT
	bool exists;
	int mismatch;

	/* It may be sufficient for you application to check whether the correct
	 * certificate is provisioned with a given tag directly using
	 * modem_key_mgmt_cmp(). Here, for the sake of the completeness, we
	 * check that a certificate exists before comparing it with what we
	 * expect it to be.
	 */
	err = modem_key_mgmt_exists(TLS_SEC_TAG,
				    MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN, &exists);
	if (err) {
		LOG_ERR("Failed to check for certificates err %d", err);
		return err;
	}

	if (exists) {
		mismatch = modem_key_mgmt_cmp(TLS_SEC_TAG,
					      MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN,
					      cert, sizeof(cert));
		if (!mismatch) {
			LOG_INF("Certificate match");
			return 0;
		}

		LOG_INF("Certificate mismatch");
		err = modem_key_mgmt_delete(TLS_SEC_TAG,
					    MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN);
		if (err) {
			LOG_ERR("Failed to delete existing certificate, err %d",
				err);
		}
	}

	LOG_INF("Provisioning certificate to the modem");

	/*  Provision certificate to the modem */
	err = modem_key_mgmt_write(TLS_SEC_TAG,
				   MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN, cert,
				   sizeof(cert));
	if (err) {
		LOG_ERR("Failed to provision certificate, err %d", err);
		return err;
	}
#else  /* CONFIG_MODEM_KEY_MGMT */
	err = tls_credential_add(TLS_SEC_TAG, TLS_CREDENTIAL_CA_CERTIFICATE,
				 cert, sizeof(cert));
	if (err == -EEXIST) {
		LOG_INF("CA certificate already exists, sec tag: %d",
			TLS_SEC_TAG);
	} else if (err < 0) {
		LOG_ERR("Failed to register CA certificate: %d", err);
		return err;
	}
#endif /* !CONFIG_MODEM_KEY_MGMT */

	return 0;
}

/* Setup TLS options on a given socket */
static int tls_setup(int fd)
{
	int err;
	int verify;

	/* Security tag that we have provisioned the certificate with */
	const sec_tag_t tls_sec_tag[] = {
		TLS_SEC_TAG,
	};

	/* Set up TLS peer verification */
	enum {
		NONE = 0,
		OPTIONAL = 1,
		REQUIRED = 2,
	};

	verify = REQUIRED;

	err = setsockopt(fd, SOL_TLS, TLS_PEER_VERIFY, &verify, sizeof(verify));
	if (err) {
		LOG_ERR("Failed to setup peer verification, err %d", errno);
		return err;
	}

	/* Associate the socket with the security tag
	 * we have provisioned the certificate with.
	 */
	err = setsockopt(fd, SOL_TLS, TLS_SEC_TAG_LIST, tls_sec_tag,
			 sizeof(tls_sec_tag));
	if (err) {
		LOG_ERR("Failed to setup TLS sec tag, err %d", errno);
		return err;
	}

	err = setsockopt(fd, SOL_TLS, TLS_HOSTNAME, CONFIG_APP_HTTPS_HOSTNAME,
			 sizeof(CONFIG_APP_HTTPS_HOSTNAME) - 1);
	if (err) {
		LOG_ERR("Failed to setup TLS hostname, err %d", errno);
		return err;
	}
	return 0;
}

static bool check_dns_ready(const char *hostname)
{
	int err;
	struct addrinfo *res = NULL;
	struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
		.ai_flags = AI_NUMERICSERV,
	};

	err = getaddrinfo(hostname, HTTPS_PORT, &hints, &res);
	if (res) {
		freeaddrinfo(res);
	}

	return (err == 0);
}

static void send_http_request(void)
{
	int err;
	int fd = -1;
	char *p;
	int bytes;
	size_t off;
	struct addrinfo *res = NULL;
	struct addrinfo hints = {
		.ai_flags = AI_NUMERICSERV, /* Let getaddrinfo() set port */
		.ai_socktype = SOCK_STREAM,
		.ai_family = AF_INET, /* Force IPv4 to reduce DNS lookup time */
	};
	char peer_addr[INET6_ADDRSTRLEN];
	bool request_failed = false;

	if (!network_ready) {
		LOG_WRN("Network not ready, skipping HTTPS request");
		return;
	}

	/* Increment total request count (both local and Memfault) */
	https_req_total++;
	MEMFAULT_METRIC_SET_UNSIGNED(app_https_req_total_count,
				     https_req_total);

	LOG_INF("Looking up %s", CONFIG_APP_HTTPS_HOSTNAME);

	err = getaddrinfo(CONFIG_APP_HTTPS_HOSTNAME, HTTPS_PORT, &hints, &res);
	if (err) {
		LOG_ERR("getaddrinfo() failed, err %d", errno);
		request_failed = true;
		goto clean_up;
	}

	inet_ntop(res->ai_family,
		  &((struct sockaddr_in *)(res->ai_addr))->sin_addr, peer_addr,
		  INET6_ADDRSTRLEN);
	LOG_INF("Resolved %s (%s)", peer_addr, net_family2str(res->ai_family));

	if (IS_ENABLED(CONFIG_SAMPLE_TFM_MBEDTLS)) {
		fd = socket(res->ai_family, SOCK_STREAM | SOCK_NATIVE_TLS,
			    IPPROTO_TLS_1_2);
	} else {
		fd = socket(res->ai_family, SOCK_STREAM, IPPROTO_TLS_1_2);
	}
	if (fd == -1) {
		LOG_ERR("socket() failed, err %d", errno);
		request_failed = true;
		goto clean_up;
	}

	/* Set socket timeouts to prevent hanging */
	struct timeval timeout = {
		.tv_sec = 30,
		.tv_usec = 0,
	};
	(void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
			 sizeof(timeout));
	(void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout,
			 sizeof(timeout));

	/* Setup TLS socket options */
	err = tls_setup(fd);
	if (err) {
		LOG_ERR("TLS setup failed");
		request_failed = true;
		goto clean_up;
	}

	LOG_INF("Connecting to %s:%d", CONFIG_APP_HTTPS_HOSTNAME,
		ntohs(((struct sockaddr_in *)(res->ai_addr))->sin_port));
	err = connect(fd, res->ai_addr, res->ai_addrlen);
	if (err) {
		LOG_ERR("connect() failed, err: %d", errno);
		request_failed = true;
		goto clean_up;
	}

	off = 0;
	do {
		bytes = send(fd, &send_buf[off], HTTP_HEAD_LEN - off, 0);
		if (bytes < 0) {
			LOG_ERR("send() failed, err %d", errno);
			request_failed = true;
			goto clean_up;
		}
		off += bytes;
	} while (off < HTTP_HEAD_LEN);

	LOG_INF("Sent %d bytes", off);

	off = 0;
	do {
		bytes = recv(fd, &recv_buf[off], RECV_BUF_SIZE - off, 0);
		if (bytes < 0) {
			LOG_ERR("recv() failed, err %d", errno);
			request_failed = true;
			goto clean_up;
		}
		off += bytes;
	} while (bytes != 0 /* peer closed connection */);

	LOG_INF("Received %d bytes", off);

	/* Make sure recv_buf is NULL terminated (for safe use with strstr) */
	if (off < sizeof(recv_buf)) {
		recv_buf[off] = '\0';
	} else {
		recv_buf[sizeof(recv_buf) - 1] = '\0';
	}

	/* Print HTTP response */
	p = strstr(recv_buf, "\r\n");
	if (p) {
		off = p - recv_buf;
		recv_buf[off + 1] = '\0';
		LOG_INF("Response: %s", recv_buf);
	}

	LOG_DBG("Finished, closing socket");

clean_up:
	if (request_failed) {
		https_req_failures++;
		MEMFAULT_METRIC_SET_UNSIGNED(app_https_req_fail_count,
					     https_req_failures);
	}
	/* Log local metrics after each request */
	LOG_INF("App HTTPS Request Metrics - Total: %u, Failures: %u",
		https_req_total, https_req_failures);

	if (fd >= 0) {
		/* Graceful shutdown - notify peer we're done sending */
		(void)zsock_shutdown(fd, ZSOCK_SHUT_RDWR);
		(void)close(fd);
		/* Small delay to allow TCP/TLS resources to be released */
		k_sleep(K_MSEC(100));
	}
	if (res) {
		freeaddrinfo(res);
	}
}

static void app_https_client_thread(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	uint32_t http_request_count = 1;
	int cert_provisioned = 0;

	LOG_INF("App HTTPS client thread started");

	while (https_client_running) {
		/* Wait for network connection */
		k_sem_take(&https_thread_sem, K_FOREVER);

		if (!https_client_running) {
			break;
		}

		/* Verify network is actually ready */
		if (!network_ready) {
			LOG_WRN("Network not ready after semaphore signal, "
				"skipping");
			continue;
		}

		/* Provision certificates once when first connected */
		if (!cert_provisioned) {
			int err = cert_provision();
			if (err) {
				LOG_ERR("Certificate provisioning failed: %d",
					err);
				network_ready = false;
				continue;
			}
			cert_provisioned = 1;
			LOG_INF("Certificate provisioned successfully");
		}

		LOG_INF("Network ready, waiting for DNS resolver");

		/* Wait for DNS to be ready - check every 10 seconds, timeout
		 * after 5 minutes */
		int dns_wait_time = 0;
		const int dns_timeout = 300; /* 5 minutes */
		while (network_ready &&
		       !check_dns_ready(CONFIG_APP_HTTPS_HOSTNAME)) {
			if (dns_wait_time >= dns_timeout) {
				LOG_ERR("DNS timeout after %d seconds for %s, "
					"continuing anyway",
					dns_timeout, CONFIG_APP_HTTPS_HOSTNAME);
				break;
			}
			LOG_INF("DNS not ready for %s, checking again in %d "
				"seconds",
				CONFIG_APP_HTTPS_HOSTNAME,
				DNS_CHECK_INTERVAL_SEC);
			k_sleep(K_SECONDS(DNS_CHECK_INTERVAL_SEC));
			dns_wait_time += DNS_CHECK_INTERVAL_SEC;
		}

		/* Verify network still ready after DNS checks */
		if (!network_ready) {
			LOG_WRN("Network disconnected during DNS wait, "
				"aborting");
			continue;
		}

		if (dns_wait_time < dns_timeout) {
			LOG_INF("DNS ready for %s after %d seconds, sending "
				"HTTPS requests every %d seconds",
				CONFIG_APP_HTTPS_HOSTNAME, dns_wait_time,
				HTTPS_REQUEST_INTERVAL_SEC);
		} else {
			LOG_WRN("Starting HTTPS operations without DNS "
				"confirmation");
		}
		while (https_client_running && network_ready) {
			send_http_request();
			LOG_INF("HTTP request count: %d", http_request_count++);

			/* Sleep for the configured interval */
			k_sleep(K_SECONDS(HTTPS_REQUEST_INTERVAL_SEC));
		}

		LOG_INF("Network disconnected or client stopped");
	}

	LOG_INF("App HTTPS client thread exiting");
}

K_THREAD_DEFINE(app_https_client_tid, CONFIG_APP_HTTPS_CLIENT_STACK_SIZE,
		app_https_client_thread, NULL, NULL, NULL,
		CONFIG_APP_HTTPS_CLIENT_THREAD_PRIORITY, 0, 0);

int app_https_client_init(void)
{
	LOG_INF("App HTTPS client initialized");
	https_client_running = true;
	return 0;
}

static int app_https_client_module_init(void)
{
	return app_https_client_init();
}
SYS_INIT(app_https_client_module_init, APPLICATION, 2);

void app_https_client_notify_connected(void)
{
	if (https_client_running && !network_ready) {
		LOG_INF("Network connected, notifying app HTTPS client");
		network_ready = true;
		k_sem_give(&https_thread_sem);
	} else if (network_ready) {
		LOG_DBG("Network already marked as ready, skipping duplicate "
			"notification");
	}
}

void app_https_client_notify_disconnected(void)
{
	LOG_INF("Network disconnected, pausing app HTTPS client");
	network_ready = false;
}

/* Zbus: react to WiFi connect/disconnect from wifi module */
extern const struct zbus_channel WIFI_CHAN;

static void app_https_wifi_listener(const struct zbus_channel *chan)
{
	const struct wifi_msg *msg = zbus_chan_const_msg(chan);

	if (msg->type == WIFI_STA_CONNECTED) {
		app_https_client_notify_connected();
	} else if (msg->type == WIFI_STA_DISCONNECTED) {
		app_https_client_notify_disconnected();
	}
}

ZBUS_LISTENER_DEFINE(app_https_wifi_listener_def, app_https_wifi_listener);
ZBUS_CHAN_ADD_OBS(WIFI_CHAN, app_https_wifi_listener_def, 0);
