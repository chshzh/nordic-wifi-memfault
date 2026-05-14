if(CONFIG_NTP_MODULE)
target_sources(app PRIVATE ntp_sync.c)
endif()
