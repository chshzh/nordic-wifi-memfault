# App-specific UX shim: overrides two of zego/bricks/ux's __weak Button 0
# gesture hooks (single-click, long-press) to avoid conflicting with this
# app's own button-driven demos (heartbeat/CDR, stack overflow). Double-click
# is intentionally left at the zego/ux default.
target_sources(app PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/ux.c
)
