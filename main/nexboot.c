/*
 * Copyright (c) 2018- Franco (nextime) Lanza <franco@nexlab.it>
 * Nexboot [https://git.nexlab.net/esp/nexboot]
 * This file is part of Nexboot.
 * Nexboot is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_spi_flash.h"
#include "esp_ota_ops.h"

#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

//#include "https_server.h"
#include "mongoose.h"
#include "html.h"

/* You can set Wifi configuration via
   'make menuconfig'.

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define WIFI_SSID "mywifissid"
*/
#define ESP_WIFI_MODE_AP   CONFIG_ESP_WIFI_MODE_AP //TRUE:AP FALSE:STA
#define ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define MAX_STA_CONN       CONFIG_MAX_STA_CONN

#define MG_LISTEN_ADDR "80"

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t wifi_event_group;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const int WIFI_CONNECTED_BIT = BIT0;

static const char *TAG = "Nexboot";

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        ESP_LOGI(TAG, "got ip:%s",
                 ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        break;
    case SYSTEM_EVENT_AP_STACONNECTED:
        ESP_LOGI(TAG, "station:"MACSTR" join, AID=%d",
                 MAC2STR(event->event_info.sta_connected.mac),
                 event->event_info.sta_connected.aid);
        break;
    case SYSTEM_EVENT_AP_STADISCONNECTED:
        ESP_LOGI(TAG, "station:"MACSTR"leave, AID=%d",
                 MAC2STR(event->event_info.sta_disconnected.mac),
                 event->event_info.sta_disconnected.aid);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        break;
    default:
        break;
    }
    return ESP_OK;
}

#if ESP_WIFI_MODE_AP
void wifi_init_softap()
{
    wifi_event_group = xEventGroupCreate();

    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = ESP_WIFI_SSID,
            .ssid_len = strlen(ESP_WIFI_SSID),
            .password = ESP_WIFI_PASS,
            .max_connection = MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    if (strlen(ESP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished.SSID:%s password:%s",
             ESP_WIFI_SSID, ESP_WIFI_PASS);
}

#else // if ESP_WIFI_MODE_AP

void wifi_init_sta()
{
    wifi_event_group = xEventGroupCreate();

    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL) );

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = ESP_WIFI_SSID,
            .password = ESP_WIFI_PASS
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");
    ESP_LOGI(TAG, "connect to ap SSID:%s password:%s",
             ESP_WIFI_SSID, ESP_WIFI_PASS);
}

#endif // if ESP_WIFI_MODE_AP


// Convert a mongoose string to a string
char *mgStrToStr(struct mg_str mgStr) {
	char *retStr = (char *) malloc(mgStr.len + 1);
	memcpy(retStr, mgStr.p, mgStr.len);
	retStr[mgStr.len] = 0;
	return retStr;
} 

struct fwriter_data {
   const esp_partition_t *update_partition;
   esp_ota_handle_t update_handle;
   size_t bytes_written;
};


static void mg_ev_handler(struct mg_connection *nc, int ev, void *p) {
  struct fwriter_data *data = (struct fwriter_data *) nc->user_data;
  struct mg_http_multipart_part *mp = (struct mg_http_multipart_part *) p;

  switch (ev) {
    case MG_EV_ACCEPT: {
      char addr[32];
      mg_sock_addr_to_str(&nc->sa, addr, sizeof(addr),
                          MG_SOCK_STRINGIFY_IP | MG_SOCK_STRINGIFY_PORT);
      ESP_LOGI(TAG, "Connection %p from %s\n", nc, addr);
      break;
    }
    case MG_EV_HTTP_REQUEST: {
      char addr[32];
      struct http_message *hm = (struct http_message *) p;
		char *uri = mgStrToStr(hm->uri);
      char *method = mgStrToStr(hm->method);
      mg_sock_addr_to_str(&nc->sa, addr, sizeof(addr),
                          MG_SOCK_STRINGIFY_IP | MG_SOCK_STRINGIFY_PORT);
      ESP_LOGI(TAG, "HTTP request from %s: %.*s %.*s\n", addr, (int) hm->method.len,
             hm->method.p, (int) hm->uri.len, hm->uri.p);
		
		if(strcmp(uri, "/") == 0) {
         if(!strcmp(method, "POST") == 0) {
            mg_send_head(nc, 200, index_html_len, "Content-Type: text/html");
            mg_send(nc, index_html, index_html_len);
         }
      } else if(strcmp(uri, "/reboot") == 0) {
         mg_send_head(nc, 200, reboot_html_len, "Content-Type: text/html");
         mg_send(nc, reboot_html, reboot_html_len);
         ESP_LOGI(TAG,"Rebooting... ");
         esp_restart();

		} else {
			mg_send_head(nc, 404, 0, "Content-Type: text/plain");
		}
      nc->flags |= MG_F_SEND_AND_CLOSE;
		free(uri);
      break;
    }
    case MG_EV_HTTP_PART_BEGIN: {
       ESP_LOGI(TAG, "Starting upload file from %p\n", nc);
       if (data == NULL) {
          data = calloc(1, sizeof(struct fwriter_data));
          data->bytes_written = 0;
          data->update_partition = NULL;
          data->update_handle = 0;

          data->update_partition = esp_ota_get_next_update_partition(NULL);
          ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%x\n",
                    data->update_partition->subtype, data->update_partition->address);
          if(data->update_partition == NULL) ESP_ERROR_CHECK(ESP_FAIL);
          ESP_ERROR_CHECK(esp_ota_begin(data->update_partition, OTA_SIZE_UNKNOWN, &data->update_handle));

          ESP_LOGI(TAG, "esp_ota_begin succeeded\n");


       }
       nc->user_data = (void *) data;

       break;
    }
    case MG_EV_HTTP_PART_DATA: {

      data->bytes_written += mp->data.len;
      ESP_LOGD(TAG, "MG_EV_HTTP_PART_DATA %p len %d\n", nc, mp->data.len);
      ESP_ERROR_CHECK(esp_ota_write( data->update_handle, (void *)mp->data.p, mp->data.len));


      break;
    } 
    case MG_EV_HTTP_PART_END: {
      ESP_LOGI(TAG, "Upload done, filesize: %d\n", mp->data.len);
      mg_printf(nc,
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/plain\r\n"
                "Connection: close\r\n\r\n"
                "Written POST data to OTA partition\n\n");

      nc->flags |= MG_F_SEND_AND_CLOSE;
      nc->user_data = NULL;
      ESP_ERROR_CHECK(esp_ota_end(data->update_handle));
      ESP_ERROR_CHECK(esp_ota_set_boot_partition(data->update_partition));
      free(data);
      ESP_LOGI(TAG,"Booting update... ");
      esp_restart();
      break;
    }
    case MG_EV_CLOSE: {
       ESP_LOGI(TAG, "Connection %p closed\n", nc);
       break;
    }
  }
}


void mongooseTask(void *data) {
	ESP_LOGI(TAG, "Mongoose task starting");
	struct mg_mgr mgr;
   struct mg_connection *nc;

	ESP_LOGD(TAG, "Mongoose: Starting setup");
	mg_mgr_init(&mgr, NULL);
	ESP_LOGD(TAG, "Mongoose succesfully inited");

   nc = mg_bind(&mgr, MG_LISTEN_ADDR, mg_ev_handler);
	ESP_LOGI(TAG, "Webserver successfully bound on port %s\n", MG_LISTEN_ADDR);
	if (nc == NULL) {
		ESP_LOGE(TAG, "No connection from the mg_bind()");
		vTaskDelete(NULL);
		return;
	}
	mg_set_protocol_http_websocket(nc);

	while (1) {
		mg_mgr_poll(&mgr, 1000);
	}
} 


void app_main()
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
#if ESP_WIFI_MODE_AP
    ESP_LOGI(TAG, "ESP_WIFI_MODE_AP");
    wifi_init_softap();
#else
    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();
#endif /*ESP_WIFI_MODE_AP*/

    // Start Mongoose task
	 xTaskCreatePinnedToCore(&mongooseTask, "mongooseTask", 20000, NULL, 5, NULL,0);
    
}
