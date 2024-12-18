
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_chip_info.h"
#include "spi_flash_mmap.h"
#include "esp_flash.h"
#include "hal/efuse_hal.h"
#include "nvs_flash.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "esp_bt_device.h"

#include "sdkconfig.h"
#include "driver/gpio.h"

// #include "KEY.h"

#define TAG "GATTS_DEMO"
// #include "driver/gpio.h"
// #define KEY_GPIO		0		//按钮连接的GPIO，这里使用BOOT引脚GPIO0
// bool BluetoothConnStatus = false;
// uint8_t BLEReadDataBuff[512];

// uint8_t Current_gatts_if;
// uint16_t Current_conn_id;

bool BluetoothConnStatus = false;
uint8_t BLEReadDataBuff[512];

uint8_t Current_gatts_if;
uint16_t Current_conn_id;



// GATT 协议创建两个服务
// A、B两个服务事件处理函数，声明为静态
static void gatts_profile_a_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);
static void gatts_profile_b_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);

// 服务A用于传感器控制与读取
#define GATTS_SERVICE_UUID_TEST_A   0x00AA
#define GATTS_CHAR_UUID_TEST_A      0xAA01
#define GATTS_DESCR_UUID_TEST_A     0x3333
#define GATTS_NUM_HANDLE_TEST_A     4

// 服务B用于聊天例程
#define GATTS_SERVICE_UUID_TEST_B   0x00BB
#define GATTS_CHAR_UUID_TEST_B      0xBB01
#define GATTS_DESCR_UUID_TEST_B     0x2222
#define GATTS_NUM_HANDLE_TEST_B     4


// 蓝牙设备名
#define TEST_DEVICE_NAME			"LCKFB"
// 制造商数据长度
#define TEST_MANUFACTURER_DATA_LEN  17
// 属性值最大长度
#define GATTS_DEMO_CHAR_VAL_LEN_MAX 0x40	// 64
// 数据包缓存最大尺寸
#define PREPARE_BUF_MAX_SIZE 1024
uint8_t RevMsg[512];


static uint8_t char1_str[] = {0x11,0x22,0x33};
static esp_gatt_char_prop_t a_property = 0;
static esp_gatt_char_prop_t b_property = 0;

static esp_attr_value_t gatts_demo_char1_val =
{
    .attr_max_len = GATTS_DEMO_CHAR_VAL_LEN_MAX,
    .attr_len     = sizeof(char1_str),
    .attr_value   = char1_str,
};

static uint8_t adv_config_done = 0;
#define adv_config_flag      (1 << 0)
#define scan_rsp_config_flag (1 << 1)

#ifdef CONFIG_SET_RAW_ADV_DATA
static uint8_t raw_adv_data[] = {
        0x02, 0x01, 0x06,0x02, 0x0a, 0xeb, 0x03, 0x03, 0xab, 0xcd
};
static uint8_t raw_scan_rsp_data[] = {
        0x0f, 0x09, 0x45, 0x53, 0x50, 0x5f, 0x47, 0x41, 0x54, 0x54, 0x53, 0x5f, 0x44,0x45, 0x4d, 0x4f
};
#else

static uint8_t adv_service_uuid128[32] = {
    /* LSB <--------------------------------------------------------------------------------> MSB */
    //first uuid, 16bit, [12],[13] is the value
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xAA, 0x00, 0x00, 0x00,
    //second uuid, 32bit, [12], [13], [14], [15] is the value
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xBB, 0x00, 0x00, 0x00,
};

// The length of adv data must be less than 31 bytes
//static uint8_t test_manufacturer[TEST_MANUFACTURER_DATA_LEN] =  {0x12, 0x23, 0x45, 0x56};
//adv data
static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = false,
    .min_interval = 0x0006, //slave connection min interval, Time = min_interval * 1.25 msec
    .max_interval = 0x0010, //slave connection max interval, Time = max_interval * 1.25 msec
    .appearance = 0x00,
    .manufacturer_len = 0, //TEST_MANUFACTURER_DATA_LEN,
    .p_manufacturer_data =  NULL, //&test_manufacturer[0],
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(adv_service_uuid128),
    .p_service_uuid = adv_service_uuid128,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};
// scan response data
static esp_ble_adv_data_t scan_rsp_data = {
    .set_scan_rsp = true,
    .include_name = true,
    .include_txpower = true,
    //.min_interval = 0x0006,
    //.max_interval = 0x0010,
    .appearance = 0x00,
    .manufacturer_len = 0, //TEST_MANUFACTURER_DATA_LEN,
    .p_manufacturer_data =  NULL, //&test_manufacturer[0],
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(adv_service_uuid128),
    .p_service_uuid = adv_service_uuid128,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

#endif /* CONFIG_SET_RAW_ADV_DATA */

static esp_ble_adv_params_t adv_params = {
    .adv_int_min        = 0x40,
    .adv_int_max        = 0x40,
    .adv_type           = ADV_TYPE_IND,//,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    //.peer_addr            =
    //.peer_addr_type       =
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

#define PROFILE_NUM 2
#define PROFILE_A_APP_ID 0
#define PROFILE_B_APP_ID 1

struct gatts_profile_inst {
    esp_gatts_cb_t gatts_cb;
    uint16_t gatts_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_handle;
    esp_gatt_srvc_id_t service_id;
    uint16_t char_handle;
    esp_bt_uuid_t char_uuid;
    esp_gatt_perm_t perm;
    esp_gatt_char_prop_t property;
    uint16_t descr_handle;
    esp_bt_uuid_t descr_uuid;
};

/* One gatt-based profile one app_id and one gatts_if, this array will store the gatts_if returned by ESP_GATTS_REG_EVT */
static struct gatts_profile_inst gl_profile_tab[PROFILE_NUM] = {
    [PROFILE_A_APP_ID] = {
        .gatts_cb = gatts_profile_a_event_handler,
        .gatts_if = ESP_GATT_IF_NONE,       /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
    },
    [PROFILE_B_APP_ID] = {
        .gatts_cb = gatts_profile_b_event_handler,                   /* This demo does not implement, similar as profile A */
        .gatts_if = ESP_GATT_IF_NONE,       /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
    },
};

typedef struct {
    uint8_t                 *prepare_buf;
    int                     prepare_len;
} prepare_type_env_t;

static prepare_type_env_t a_prepare_write_env;
static prepare_type_env_t b_prepare_write_env;

void example_write_event_env(esp_gatt_if_t gatts_if, prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param);
void example_exec_write_event_env(prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param);

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
	switch (event) {
#ifdef CONFIG_SET_RAW_ADV_DATA
	case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
		adv_config_done &= (~adv_config_flag);
		if (adv_config_done==0){
			esp_ble_gap_start_advertising(&adv_params);
		}
		break;
	case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
		adv_config_done &= (~scan_rsp_config_flag);
		if (adv_config_done==0){
			esp_ble_gap_start_advertising(&adv_params);
		}
		break;
#else
	case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
		adv_config_done &= (~adv_config_flag);
		if (adv_config_done == 0){
			esp_ble_gap_start_advertising(&adv_params);
		}
		break;
	case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
		adv_config_done &= (~scan_rsp_config_flag);
		if (adv_config_done == 0){
			esp_ble_gap_start_advertising(&adv_params);
		}
		break;
#endif
	case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
		//advertising start complete event to indicate advertising start successfully or failed
		if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
			ESP_LOGE(TAG, "Advertising start failed\n");
		}
		break;
	case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
		if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS) {
			ESP_LOGE(TAG, "Advertising stop failed\n");
		} else {
			ESP_LOGI(TAG, "Stop adv successfully\n");
		}
		break;
	case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
		ESP_LOGI(TAG, "update connection params status = %d, min_int = %d, max_int = %d,conn_int = %d,latency = %d, timeout = %d",
				param->update_conn_params.status,
				param->update_conn_params.min_int,
				param->update_conn_params.max_int,
				param->update_conn_params.conn_int,
				param->update_conn_params.latency,
				param->update_conn_params.timeout);
		break;
	default:
		break;
	}
}

void example_write_event_env(esp_gatt_if_t gatts_if, prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param){
	esp_gatt_status_t status = ESP_GATT_OK;
	if (param->write.need_rsp){
		if (param->write.is_prep){
			if (prepare_write_env->prepare_buf == NULL) {
				prepare_write_env->prepare_buf = (uint8_t *)malloc(PREPARE_BUF_MAX_SIZE*sizeof(uint8_t));
				prepare_write_env->prepare_len = 0;
				if (prepare_write_env->prepare_buf == NULL) {
					ESP_LOGE(TAG, "Gatt_server prep no mem\n");
					status = ESP_GATT_NO_RESOURCES;
				}
			} else {
				if(param->write.offset > PREPARE_BUF_MAX_SIZE) {
					status = ESP_GATT_INVALID_OFFSET;
				} else if ((param->write.offset + param->write.len) > PREPARE_BUF_MAX_SIZE) {
					status = ESP_GATT_INVALID_ATTR_LEN;
				}
			}

			esp_gatt_rsp_t *gatt_rsp = (esp_gatt_rsp_t *)malloc(sizeof(esp_gatt_rsp_t));
			gatt_rsp->attr_value.len = param->write.len;
			gatt_rsp->attr_value.handle = param->write.handle;
			gatt_rsp->attr_value.offset = param->write.offset;
			gatt_rsp->attr_value.auth_req = ESP_GATT_AUTH_REQ_NONE;
			memcpy(gatt_rsp->attr_value.value, param->write.value, param->write.len);
			esp_err_t response_err = esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, status, gatt_rsp);
			if (response_err != ESP_OK){
			ESP_LOGE(TAG, "Send response error\n");
			}
			free(gatt_rsp);
			if (status != ESP_GATT_OK){
				return;
			}
			memcpy(prepare_write_env->prepare_buf + param->write.offset,
				param->write.value,
				param->write.len);
			prepare_write_env->prepare_len += param->write.len;

		}else{
			esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, status, NULL);
		}
	}
}

void example_exec_write_event_env(prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param){
	if (param->exec_write.exec_write_flag == ESP_GATT_PREP_WRITE_EXEC){
		esp_log_buffer_hex(TAG, prepare_write_env->prepare_buf, prepare_write_env->prepare_len);
	}else{
		ESP_LOGI(TAG,"ESP_GATT_PREP_WRITE_CANCEL");
	}
	if (prepare_write_env->prepare_buf) {
		free(prepare_write_env->prepare_buf);
		prepare_write_env->prepare_buf = NULL;
	}
	prepare_write_env->prepare_len = 0;
}

static void gatts_profile_a_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
	switch (event) {
	case ESP_GATTS_REG_EVT:
		ESP_LOGI(TAG, "REGISTER_APP_EVT, status %d, app_id %d\n", param->reg.status, param->reg.app_id);
		gl_profile_tab[PROFILE_A_APP_ID].service_id.is_primary = true;
		gl_profile_tab[PROFILE_A_APP_ID].service_id.id.inst_id = 0x00;
		gl_profile_tab[PROFILE_A_APP_ID].service_id.id.uuid.len = ESP_UUID_LEN_16;
		gl_profile_tab[PROFILE_A_APP_ID].service_id.id.uuid.uuid.uuid16 = GATTS_SERVICE_UUID_TEST_A;

		esp_err_t set_dev_name_ret = esp_ble_gap_set_device_name(TEST_DEVICE_NAME);
		if (set_dev_name_ret){
			ESP_LOGE(TAG, "set device name failed, error code = %x", set_dev_name_ret);
		}
#ifdef CONFIG_SET_RAW_ADV_DATA
		esp_err_t raw_adv_ret = esp_ble_gap_config_adv_data_raw(raw_adv_data, sizeof(raw_adv_data));
		if (raw_adv_ret){
			ESP_LOGE(TAG, "config raw adv data failed, error code = %x ", raw_adv_ret);
		}
		adv_config_done |= adv_config_flag;
		esp_err_t raw_scan_ret = esp_ble_gap_config_scan_rsp_data_raw(raw_scan_rsp_data, sizeof(raw_scan_rsp_data));
		if (raw_scan_ret){
			ESP_LOGE(TAG, "config raw scan rsp data failed, error code = %x", raw_scan_ret);
		}
		adv_config_done |= scan_rsp_config_flag;
#else
		//config adv data
		esp_err_t ret = esp_ble_gap_config_adv_data(&adv_data);
		if (ret){
			ESP_LOGE(TAG, "config adv data failed, error code = %x", ret);
		}
		adv_config_done |= adv_config_flag;
		//config scan response data
		ret = esp_ble_gap_config_adv_data(&scan_rsp_data);
		if (ret){
			ESP_LOGE(TAG, "config scan response data failed, error code = %x", ret);
		}
		adv_config_done |= scan_rsp_config_flag;

#endif
		esp_ble_gatts_create_service(gatts_if, &gl_profile_tab[PROFILE_A_APP_ID].service_id, GATTS_NUM_HANDLE_TEST_A);
		break;
	case ESP_GATTS_READ_EVT: {// GATTS_服务器请求读取客户端信息_事件
		esp_gatt_rsp_t rsp;							// 定义一个远程请求读取响应结构体
		memset(&rsp, 0, sizeof(esp_gatt_rsp_t));	// 清空
		rsp.attr_value.handle = param->read.handle;	// 读取句柄
		rsp.attr_value.value[0] = 0x12;
		rsp.attr_value.value[1] = 0xAB;
		rsp.attr_value.len = 2;
		// 发送响应：GATT服务器访问接口，连接标识符，传输id，响应状态，响应数据
		esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id,ESP_GATT_OK, &rsp);
		break;
	}
	// GATT 写入事件（收到手机发送来的数据）
	case ESP_GATTS_WRITE_EVT: {
		ESP_LOGI(TAG, "GATT_WRITE_EVT, conn_id %d, trans_id %ld, handle %d", param->write.conn_id, param->write.trans_id, param->write.handle);
		if (!param->write.is_prep){
			ESP_LOGI(TAG, "GATT_WRITE_EVT, value len %d, value :", param->write.len);
			// 打印收到的数据，16进制方式

			// 设置WS2812_RGB颜色
			//SetWS2812(param->write.value[0],param->write.value[1],param->write.value[2]);

			esp_log_buffer_hex(TAG, param->write.value, param->write.len);
			if (gl_profile_tab[PROFILE_A_APP_ID].descr_handle == param->write.handle && param->write.len == 2){
				uint16_t descr_value = param->write.value[1]<<8 | param->write.value[0];
				if (descr_value == 0x0001){
					if (a_property & ESP_GATT_CHAR_PROP_BIT_NOTIFY){
						ESP_LOGI(TAG, "notify enable");
						uint8_t notify_data[15];
						for (int i = 0; i < sizeof(notify_data); ++i){
							notify_data[i] = i%0xff;
						}
						//the size of notify_data[] need less than MTU size
						esp_ble_gatts_send_indicate(gatts_if, param->write.conn_id, gl_profile_tab[PROFILE_A_APP_ID].char_handle,
												sizeof(notify_data), notify_data, false);
					}
				}else if (descr_value == 0x0002){
					if (a_property & ESP_GATT_CHAR_PROP_BIT_INDICATE){
						ESP_LOGI(TAG, "indicate enable");
						uint8_t indicate_data[15];
						for (int i = 0; i < sizeof(indicate_data); ++i)
						{
							indicate_data[i] = i%0xff;
						}
						//the size of indicate_data[] need less than MTU size
						esp_ble_gatts_send_indicate(gatts_if, param->write.conn_id, gl_profile_tab[PROFILE_A_APP_ID].char_handle,
												sizeof(indicate_data), indicate_data, true);
					}
				}
				else if (descr_value == 0x0000){
					ESP_LOGI(TAG, "notify/indicate disable ");
				}else{
					ESP_LOGE(TAG, "unknown descr value");
					esp_log_buffer_hex(TAG, param->write.value, param->write.len);
				}
			}
		}
		example_write_event_env(gatts_if, &a_prepare_write_env, param);
		break;
	}
	case ESP_GATTS_EXEC_WRITE_EVT:
		ESP_LOGI(TAG,"ESP_GATTS_EXEC_WRITE_EVT");
		esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
		example_exec_write_event_env(&a_prepare_write_env, param);
		break;
	case ESP_GATTS_MTU_EVT://	设置mtu完成完成事件
		ESP_LOGI(TAG, "ESP_GATTS_MTU_EVT, MTU %d", param->mtu.mtu);
		break;
	case ESP_GATTS_UNREG_EVT:
		break;
	case ESP_GATTS_CREATE_EVT:// 创建服务完成事件
		ESP_LOGI(TAG, "CREATE_SERVICE_EVT, status %d,  service_handle %d\n", param->create.status, param->create.service_handle);
		gl_profile_tab[PROFILE_A_APP_ID].service_handle = param->create.service_handle;
		gl_profile_tab[PROFILE_A_APP_ID].char_uuid.len = ESP_UUID_LEN_16;
		gl_profile_tab[PROFILE_A_APP_ID].char_uuid.uuid.uuid16 = GATTS_CHAR_UUID_TEST_A;

		esp_ble_gatts_start_service(gl_profile_tab[PROFILE_A_APP_ID].service_handle);
		a_property = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
		esp_err_t add_char_ret = esp_ble_gatts_add_char(gl_profile_tab[PROFILE_A_APP_ID].service_handle, &gl_profile_tab[PROFILE_A_APP_ID].char_uuid,
														ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
														a_property,
														&gatts_demo_char1_val, NULL);
		if (add_char_ret){
			ESP_LOGE(TAG, "add char failed, error code =%x",add_char_ret);
		}
		break;
	case ESP_GATTS_ADD_INCL_SRVC_EVT:
		break;
	case ESP_GATTS_ADD_CHAR_EVT: {
		uint16_t length = 0;
		const uint8_t *prf_char;

		ESP_LOGI(TAG, "ADD_CHAR_EVT, status %d,  attr_handle %d, service_handle %d\n",
				param->add_char.status, param->add_char.attr_handle, param->add_char.service_handle);
		gl_profile_tab[PROFILE_A_APP_ID].char_handle = param->add_char.attr_handle;
		gl_profile_tab[PROFILE_A_APP_ID].descr_uuid.len = ESP_UUID_LEN_16;
		gl_profile_tab[PROFILE_A_APP_ID].descr_uuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
		esp_err_t get_attr_ret = esp_ble_gatts_get_attr_value(param->add_char.attr_handle,  &length, &prf_char);
		if (get_attr_ret == ESP_FAIL){
			ESP_LOGE(TAG, "ILLEGAL HANDLE");
		}

		ESP_LOGI(TAG, "the gatts demo char length = %x\n", length);
		for(int i = 0; i < length; i++){
			ESP_LOGI(TAG, "prf_char[%x] =%x\n",i,prf_char[i]);
		}
		esp_err_t add_descr_ret = esp_ble_gatts_add_char_descr(gl_profile_tab[PROFILE_A_APP_ID].service_handle, &gl_profile_tab[PROFILE_A_APP_ID].descr_uuid,
																ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, NULL, NULL);
		if (add_descr_ret){
			ESP_LOGE(TAG, "add char descr failed, error code =%x", add_descr_ret);
		}
		break;
	}
	case ESP_GATTS_ADD_CHAR_DESCR_EVT:
		gl_profile_tab[PROFILE_A_APP_ID].descr_handle = param->add_char_descr.attr_handle;
		ESP_LOGI(TAG, "ADD_DESCR_EVT, status %d, attr_handle %d, service_handle %d\n",
				param->add_char_descr.status, param->add_char_descr.attr_handle, param->add_char_descr.service_handle);
		break;
	case ESP_GATTS_DELETE_EVT:
		break;
	case ESP_GATTS_START_EVT:
		ESP_LOGI(TAG, "SERVICE_START_EVT, status %d, service_handle %d\n",
				param->start.status, param->start.service_handle);
		break;
	case ESP_GATTS_STOP_EVT:
		break;
		// 客户端连接事件
	case ESP_GATTS_CONNECT_EVT: {
		esp_ble_conn_update_params_t conn_params = {0};
		memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
		/* For the IOS system, please reference the apple official documents about the ble connection parameters restrictions. */
		conn_params.latency = 0;
		conn_params.max_int = 0x20;    // max_int = 0x20*1.25ms = 40ms
		conn_params.min_int = 0x10;    // min_int = 0x10*1.25ms = 20ms
		conn_params.timeout = 400;    // timeout = 400*10ms = 4000ms
		ESP_LOGI(TAG, "ESP_GATTS_CONNECT_EVT, conn_id %d, remote %02x:%02x:%02x:%02x:%02x:%02x:",
				param->connect.conn_id,
				param->connect.remote_bda[0], param->connect.remote_bda[1], param->connect.remote_bda[2],
				param->connect.remote_bda[3], param->connect.remote_bda[4], param->connect.remote_bda[5]);
		gl_profile_tab[PROFILE_A_APP_ID].conn_id = param->connect.conn_id;
		// 更新连接参数
		esp_ble_gap_update_conn_params(&conn_params);
		Current_gatts_if = gatts_if;
		Current_conn_id = param->connect.conn_id;
		BluetoothConnStatus = true;

		break;
	}
	case ESP_GATTS_DISCONNECT_EVT:
		ESP_LOGI(TAG, "ESP_GATTS_DISCONNECT_EVT, disconnect reason 0x%x", param->disconnect.reason);

		esp_ble_gap_start_advertising(&adv_params);
		BluetoothConnStatus = false;

		break;
	case ESP_GATTS_CONF_EVT:
		ESP_LOGI(TAG, "ESP_GATTS_CONF_EVT, status %d attr_handle %d", param->conf.status, param->conf.handle);
		if (param->conf.status != ESP_GATT_OK){
			esp_log_buffer_hex(TAG, param->conf.value, param->conf.len);
		}
		break;
	case ESP_GATTS_OPEN_EVT:
	case ESP_GATTS_CANCEL_OPEN_EVT:
	case ESP_GATTS_CLOSE_EVT:
	case ESP_GATTS_LISTEN_EVT:
	case ESP_GATTS_CONGEST_EVT:
	default:
		break;
	}
}

static void gatts_profile_b_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
	switch (event) {
	case ESP_GATTS_REG_EVT:
		ESP_LOGI(TAG, "REGISTER_APP_EVT, status %d, app_id %d\n", param->reg.status, param->reg.app_id);
		gl_profile_tab[PROFILE_B_APP_ID].service_id.is_primary = true;
		gl_profile_tab[PROFILE_B_APP_ID].service_id.id.inst_id = 0x00;
		gl_profile_tab[PROFILE_B_APP_ID].service_id.id.uuid.len = ESP_UUID_LEN_16;
		gl_profile_tab[PROFILE_B_APP_ID].service_id.id.uuid.uuid.uuid16 = GATTS_SERVICE_UUID_TEST_B;

		esp_ble_gatts_create_service(gatts_if, &gl_profile_tab[PROFILE_B_APP_ID].service_id, GATTS_NUM_HANDLE_TEST_B);
		break;
	case ESP_GATTS_READ_EVT: {
		ESP_LOGI(TAG, "GATT_READ_EVT, conn_id %d, trans_id %ld, handle %d\n", param->read.conn_id, param->read.trans_id, param->read.handle);
		esp_gatt_rsp_t rsp;
		memset(&rsp, 0, sizeof(esp_gatt_rsp_t));
		rsp.attr_value.handle = param->read.handle;
		rsp.attr_value.len = 4;
		rsp.attr_value.value[0] = 0xde;
		rsp.attr_value.value[1] = 0xed;
		rsp.attr_value.value[2] = 0xbe;
		rsp.attr_value.value[3] = 0xef;
		esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id,
									ESP_GATT_OK, &rsp);
		break;
	}
	case ESP_GATTS_WRITE_EVT: {
		ESP_LOGI(TAG, "GATT_WRITE_EVT, conn_id %d, trans_id %ld, handle %d\n", param->write.conn_id, param->write.trans_id, param->write.handle);
		if (!param->write.is_prep){
			//ESP_LOGI(TAG, "GATT_WRITE_EVT, value len %d, value :", param->write.len);
			if(param->write.len>0){
				memset(RevMsg,0,sizeof(RevMsg));
				memcpy(RevMsg,param->write.value,param->write.len);
				//ESP_LOGI(TAG, "GATT_WRITE_EVT, %d %d, \n", RevMsg[0],RevMsg[1]);
				ESP_LOGI(TAG, "GATT_WRITE_EVT, value len %d, value :%s\n", param->write.len,RevMsg);
			}
			esp_log_buffer_hex(TAG, param->write.value, param->write.len);
			if (gl_profile_tab[PROFILE_B_APP_ID].descr_handle == param->write.handle && param->write.len == 2){
				uint16_t descr_value= param->write.value[1]<<8 | param->write.value[0];
				if (descr_value == 0x0001){
					if (b_property & ESP_GATT_CHAR_PROP_BIT_NOTIFY){
						ESP_LOGI(TAG, "notify enable");
						uint8_t notify_data[15];
						for (int i = 0; i < sizeof(notify_data); ++i)
						{
							notify_data[i] = i%0xff;
						}
						//the size of notify_data[] need less than MTU size
						esp_ble_gatts_send_indicate(gatts_if, param->write.conn_id, gl_profile_tab[PROFILE_B_APP_ID].char_handle,
												sizeof(notify_data), notify_data, false);
					}
				}else if (descr_value == 0x0002){
					if (b_property & ESP_GATT_CHAR_PROP_BIT_INDICATE){
						ESP_LOGI(TAG, "indicate enable");
						uint8_t indicate_data[15];
						for (int i = 0; i < sizeof(indicate_data); ++i)
						{
							indicate_data[i] = i%0xff;
						}
						//the size of indicate_data[] need less than MTU size
						esp_ble_gatts_send_indicate(gatts_if, param->write.conn_id, gl_profile_tab[PROFILE_B_APP_ID].char_handle,
												sizeof(indicate_data), indicate_data, true);
					}
				}
				else if (descr_value == 0x0000){
					ESP_LOGI(TAG, "notify/indicate disable ");
				}else{
					ESP_LOGE(TAG, "unknown value");
				}

			}
		}
		example_write_event_env(gatts_if, &b_prepare_write_env, param);
		break;
	}
	case ESP_GATTS_EXEC_WRITE_EVT:
		ESP_LOGI(TAG,"ESP_GATTS_EXEC_WRITE_EVT");
		esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
		example_exec_write_event_env(&b_prepare_write_env, param);
		break;
	case ESP_GATTS_MTU_EVT:
		ESP_LOGI(TAG, "ESP_GATTS_MTU_EVT, MTU %d", param->mtu.mtu);
		break;
	case ESP_GATTS_UNREG_EVT:
		break;
	case ESP_GATTS_CREATE_EVT:
		ESP_LOGI(TAG, "CREATE_SERVICE_EVT, status %d,  service_handle %d\n", param->create.status, param->create.service_handle);
		gl_profile_tab[PROFILE_B_APP_ID].service_handle = param->create.service_handle;
		gl_profile_tab[PROFILE_B_APP_ID].char_uuid.len = ESP_UUID_LEN_16;
		gl_profile_tab[PROFILE_B_APP_ID].char_uuid.uuid.uuid16 = GATTS_CHAR_UUID_TEST_B;

		esp_ble_gatts_start_service(gl_profile_tab[PROFILE_B_APP_ID].service_handle);
		b_property = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
		esp_err_t add_char_ret =esp_ble_gatts_add_char( gl_profile_tab[PROFILE_B_APP_ID].service_handle, &gl_profile_tab[PROFILE_B_APP_ID].char_uuid,
														ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
														b_property,
														NULL, NULL);
		if (add_char_ret){
			ESP_LOGE(TAG, "add char failed, error code =%x",add_char_ret);
		}
		break;
	case ESP_GATTS_ADD_INCL_SRVC_EVT:
		break;
	case ESP_GATTS_ADD_CHAR_EVT:
		ESP_LOGI(TAG, "ADD_CHAR_EVT, status %d,  attr_handle %d, service_handle %d\n",
				param->add_char.status, param->add_char.attr_handle, param->add_char.service_handle);

		gl_profile_tab[PROFILE_B_APP_ID].char_handle = param->add_char.attr_handle;
		gl_profile_tab[PROFILE_B_APP_ID].descr_uuid.len = ESP_UUID_LEN_16;
		gl_profile_tab[PROFILE_B_APP_ID].descr_uuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
		esp_ble_gatts_add_char_descr(gl_profile_tab[PROFILE_B_APP_ID].service_handle, &gl_profile_tab[PROFILE_B_APP_ID].descr_uuid,
									ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
									NULL, NULL);
		break;
	case ESP_GATTS_ADD_CHAR_DESCR_EVT:
		gl_profile_tab[PROFILE_B_APP_ID].descr_handle = param->add_char_descr.attr_handle;
		ESP_LOGI(TAG, "ADD_DESCR_EVT, status %d, attr_handle %d, service_handle %d\n",
				param->add_char_descr.status, param->add_char_descr.attr_handle, param->add_char_descr.service_handle);
		break;
	case ESP_GATTS_DELETE_EVT:
		break;
	case ESP_GATTS_START_EVT:
		ESP_LOGI(TAG, "SERVICE_START_EVT, status %d, service_handle %d\n",
				param->start.status, param->start.service_handle);
		break;
	case ESP_GATTS_STOP_EVT:
		break;
	case ESP_GATTS_CONNECT_EVT:
		ESP_LOGI(TAG, "CONNECT_EVT, conn_id %d, remote %02x:%02x:%02x:%02x:%02x:%02x:",
				param->connect.conn_id,
				param->connect.remote_bda[0], param->connect.remote_bda[1], param->connect.remote_bda[2],
				param->connect.remote_bda[3], param->connect.remote_bda[4], param->connect.remote_bda[5]);
		gl_profile_tab[PROFILE_B_APP_ID].conn_id = param->connect.conn_id;
		break;
	case ESP_GATTS_CONF_EVT:
		ESP_LOGI(TAG, "ESP_GATTS_CONF_EVT status %d attr_handle %d", param->conf.status, param->conf.handle);
		if (param->conf.status != ESP_GATT_OK){
			esp_log_buffer_hex(TAG, param->conf.value, param->conf.len);
		}
	break;
	case ESP_GATTS_DISCONNECT_EVT:
	case ESP_GATTS_OPEN_EVT:
	case ESP_GATTS_CANCEL_OPEN_EVT:
	case ESP_GATTS_CLOSE_EVT:
	case ESP_GATTS_LISTEN_EVT:
	case ESP_GATTS_CONGEST_EVT:
	default:
		break;
	}
}

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
	/* If event is register event, store the gatts_if for each profile */
	if (event == ESP_GATTS_REG_EVT) {
		if (param->reg.status == ESP_GATT_OK) {
			gl_profile_tab[param->reg.app_id].gatts_if = gatts_if;
		} else {
			ESP_LOGI(TAG, "Reg app failed, app_id %04x, status %d\n",
					param->reg.app_id,
					param->reg.status);
			return;
		}
	}

	/* If the gatts_if equal to profile A, call profile A cb handler,
	* so here call each profile's callback */
	do {
		int idx;
		for (idx = 0; idx < PROFILE_NUM; idx++) {
			if (gatts_if == ESP_GATT_IF_NONE || /* ESP_GATT_IF_NONE, not specify a certain gatt_if, need to call every profile cb function */
					gatts_if == gl_profile_tab[idx].gatts_if) {
				if (gl_profile_tab[idx].gatts_cb) {
					gl_profile_tab[idx].gatts_cb(event, gatts_if, param);
				}
			}
		}
	} while (0);
}
// 读取按钮状态
uint8_t ReadKeyStatus(void)
{
	if(gpio_get_level(0)){
		return 0;
	}
	return 1;
}
void app_main()
{	
	char *BTMAC[16];

	uint8_t MAC[6];
	// 打印库版本信息
	ESP_LOGI(TAG, "[APP] Start!~\r\n");
	ESP_LOGI(TAG, "[APP] IDF Version is %d.%d.%d",ESP_IDF_VERSION_MAJOR,ESP_IDF_VERSION_MINOR,ESP_IDF_VERSION_PATCH);
	ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());
	// 打印芯片信息
	esp_chip_info_t chip_info;
	esp_chip_info(&chip_info);
	ESP_LOGI(TAG, "ESP32 Chip Cores Count:  %d",chip_info.cores);
	if(chip_info.model == 1){
		ESP_LOGI(TAG, "ESP32 Chip Model is:  ESP32");
	}else if(chip_info.model == 2){
		ESP_LOGI(TAG, "ESP32 Chip Model is:  ESP32S2");
	}else{
		ESP_LOGI(TAG, "ESP32 Chip Model is:  Unknown Model");
	}
	ESP_LOGI(TAG, "ESP32 Chip Features is:  %lu",chip_info.features);
	ESP_LOGI(TAG, "ESP32 Chip Revision is:  %d",chip_info.revision);

	ESP_LOGI(TAG, "ESP32 Chip, WiFi%s%s, ",
			(chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
			(chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

	uint32_t size_flash_chip;
	esp_flash_get_size(NULL, &size_flash_chip);

	ESP_LOGI(TAG, "SPI Flash Chip Size: %lu MByte %s Flash", size_flash_chip / (1024 * 1024),
			(chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "Embedded" : "External");

	ESP_LOGI(TAG, "Free Heap Size is:  %ld Byte",esp_get_free_heap_size());
	ESP_LOGI(TAG, "Free Internal Heap Size is:  %ld Byte",esp_get_free_internal_heap_size());
	ESP_LOGI(TAG, "Free minimum Heap Size is:  %ld Byte",esp_get_minimum_free_heap_size());

	efuse_hal_get_mac(MAC);
	ESP_LOGI(TAG, "MAC Address:");
	ESP_LOG_BUFFER_HEX(TAG, MAC,6);

	// 初始化flash
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES){
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);





	// 释放未使用的BT Classic内存。
	ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

	esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
	// 初始化BT控制器以分配任务和其他资源。 在调用任何其他BT函数之前，仅应调用一次此函数。
	ret = esp_bt_controller_init(&bt_cfg);
	if (ret) {
		ESP_LOGE(TAG, "%s initialize controller failed: %s\n", __func__, esp_err_to_name(ret));
		return;
	}
	// 启用BT控制器。 esp_bt_controller_disable()
	ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
	if (ret) {
		ESP_LOGE(TAG, "%s enable controller failed: %s\n", __func__, esp_err_to_name(ret));
		return;
	}
	// 初始化并为蓝牙分配资源
	ret = esp_bluedroid_init();
	if (ret) {
		ESP_LOGE(TAG, "%s init bluetooth failed: %s\n", __func__, esp_err_to_name(ret));
		return;
	}
	// 启用蓝牙，必须在esp_bluedroid_init（）之后
	ret = esp_bluedroid_enable();
	if (ret) {
		ESP_LOGE(TAG, "%s enable bluetooth failed: %s\n", __func__, esp_err_to_name(ret));
		return;
	}
		// 注册BTA GATTS模块的事件回调函数
	ret = esp_ble_gatts_register_callback(gatts_event_handler);
	if (ret){
		ESP_LOGE(TAG, "gatts register error, error code = %x", ret);
		return;
	}
	// 注册GAP事件回调函数，例如扫描结果
	ret = esp_ble_gap_register_callback(gap_event_handler);
	if (ret){
		ESP_LOGE(TAG, "gap register error, error code = %x", ret);
		return;
	}
	// 注册应用程序标识符
	ret = esp_ble_gatts_app_register(PROFILE_A_APP_ID);
	if (ret){
		ESP_LOGE(TAG, "gatts app register error, error code = %x", ret);
		return;
	}
	// 注册应用程序标识符
	ret = esp_ble_gatts_app_register(PROFILE_B_APP_ID);
	if (ret){
		ESP_LOGE(TAG, "gatts app register error, error code = %x", ret);
		return;
	}
	// 配置本地MTU大小
	esp_err_t local_mtu_ret = esp_ble_gatt_set_local_mtu(100);
	if (local_mtu_ret){
		ESP_LOGE(TAG, "set local  MTU failed, error code = %x", local_mtu_ret);
	}
	const uint8_t *addr = esp_bt_dev_get_address();
	ESP_LOGI(TAG, "BT MAC Addr :  %02X.%02X.%02X.%02X.%02X.%02X",addr[0],addr[1],addr[2],addr[3],addr[4],addr[5]);

	//WS2812B_init();		// WS2812B初始化
//	KeyInit();			// 按键GPIO初始化
	//xTaskCreate(&ADXL345_Task,"ADXL345_Task",4096,NULL,4,NULL);
    //配置GPIO结构体
	gpio_config_t io_conf;
	io_conf.intr_type = GPIO_INTR_DISABLE;		// 禁用中断
	io_conf.pin_bit_mask = 1 << 0;		        // 设置GPIO号
	io_conf.mode = GPIO_MODE_INPUT;						// 模式输入
	io_conf.pull_up_en = GPIO_PULLUP_ENABLE;	// 端口上拉使能
	gpio_config(&io_conf);

	while (1)
	{
		// 读取按钮状态
		if(ReadKeyStatus()){
			if(BluetoothConnStatus){
				memset(BLEReadDataBuff,0,sizeof(BLEReadDataBuff));
				memcpy(BLEReadDataBuff,"HelloBug i am ESP32",sizeof("HelloBug i am ESP32"));
				esp_ble_gatts_send_indicate(Current_gatts_if, Current_conn_id, gl_profile_tab[PROFILE_B_APP_ID].char_handle,
												sizeof(BLEReadDataBuff), BLEReadDataBuff, false);
				ESP_LOGI(TAG, "ReadKeyStatus down");
				vTaskDelay(300 / portTICK_PERIOD_MS);

			}
		}
		vTaskDelay(10 / portTICK_PERIOD_MS);
	}
	return;
}
