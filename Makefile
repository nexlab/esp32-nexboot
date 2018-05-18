PROJECT_NAME := nexboot
CFLAGS += -DMG_ENABLE_HTTP_STREAMING_MULTIPART
include $(IDF_PATH)/make/project.mk
clean:
	./clean.sh
