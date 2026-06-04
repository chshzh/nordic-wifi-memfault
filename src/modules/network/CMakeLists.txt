# App-specific network shim: defines WIFI_CHAN / NETWORK_CHAN and overrides
# the __weak callbacks from zego/network/src/net_event_mgmt.c.
target_sources(app PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/net_event_app.c
)
