/*
 * Copyright 2010-2015 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *  http://aws.amazon.com/apache2.0
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 *
 * Modified by Qiang Gu(Qiang.Gu@synopsys.com) for embARC
 * \version 2017.03
 * \date 2016-01-26
 */

/**
 * \defgroup	EMBARC_APP_FREERTOS_IOT_AWS_SMARTHOME_NODES_LIVINGROOM	embARC IoT Amazon Web Services(AWS) Smart Home Node Living Room
 * \ingroup	EMBARC_APPS_TOTAL
 * \ingroup	EMBARC_APPS_OS_FREERTOS
 * \ingroup	EMBARC_APPS_MID_LWIP
 * \ingroup	EMBARC_APPS_MID_FATFS
 * \ingroup	EMBARC_APPS_MID_MBEDTLS
 * \ingroup	EMBARC_APPS_MID_AWS
 * \brief	embARC IoT Amazon Web Services(AWS) Smart Home Node Living Room
 *
 * \details
 * ### Extra Required Tools
 *
 * ### Extra Required Peripherals
 *     * Digilent PMOD WIFI(MRF24WG0MA)
 *     * Digilent PMOD TMP2
 *
 * ### Design Concept
 *     The Pmod modules should be connected to \ref EMBARC_BOARD_CONNECTION "EMSK".
 *     This example is designed to show how to connect EMSK and [AWS IoT](https://aws.amazon.com/iot/?nc1=h_ls) with using embARC.
 *
 * ### Usage Manual
 *     Before compiling this example, you need to change macro **AWS_IOT_MQTT_HOST** in aws_iot_config.h to your own aws iot cloud mqtt host.
 *     And create an aws iot thing named **livingRoom**, you can refer to [Gettting Started with AWS IoT](https://docs.aws.amazon.com/iot/latest/developerguide/iot-gs.html)
 *     for details, and generate certificates and download them and rename them to the ones under aws/smarthome_nodes/cert/livingRoom.
 *
 *     Copy aws/smarthome_nodes/cert to root folder of the EMSK SD card and run this example.
 *     It can report the following parameters to the AWS IoT Shadow.
 *     - the LivingRoom Light (on or off) - Button L <-> Upper LED 2-3
 *
 *     EMSK can send the status of the LivingRoom Light status to the AWS IoT and interact with AWS IoT with using embARC.
 *     The multiple nodes of smart home can connect AWS IoT cloud simultaneously.
 *
 *     Open [dashboard website](http://foss-for-synopsys-dwc-arc-processors.github.io/freeboard/), and load dashboard-smarthome-multithings.json
 *     dashboard configuration in upper folder, and then you can control and monitor the multiple nodes.
 *
 *     After load the configuration, you also need to click at the setting icon, and then click on the aws datasource, and then
 *     change the AWS IOT ENDPOINT, REGION, ACCESS KEY, SECRET KEY to your own aws ones.
 *     ![embARC AWS Smart Home Dashboard Setting - Step1](emsk_freertos_iot_aws_smarthome_webapp_setting1.jpg)
 *     ![embARC AWS Smart Home Dashboard Setting - Step2](emsk_freertos_iot_aws_smarthome_webapp_setting2.jpg)
 *
 *     The JSON Document in the AWS IoT cloud is shown below.
 *
 *     \code{.unparsed}
 *     {
 *       "reported": {
 *         "LightsOn": false
 *       },
 *       "desired": {
 *         "LightsOn": false
 *       }
 *     }
 *     \endcode
 *
 *     ![ScreenShot of embARC AWS Smart Home Demo using EMSK](/doc/documents/pic/images/example/emsk/emsk_freertos_iot_aws_smarthome_nodes.jpg)
 *     ![ScreenShot of embARC AWS Smart Home Kitchen Node using EMSK](/doc/documents/pic/images/example/emsk/emsk_freertos_iot_aws_smarthome_nodes_livingroom_uart.jpg)
 *     ![ScreenShot of embARC IoT Web App](/doc/documents/pic/images/example/emsk/emsk_freertos_iot_aws_smarthome_nodes_livingroom_webapp.jpg)
 *
 * ### Extra Comments
 *
 */

/**
 * \file
 * \ingroup	EMBARC_APP_FREERTOS_IOT_AWS_SMARTHOME_NODES_LIVINGROOM
 * \brief	embARC IoT Amazon Web Services(AWS) smart home node living room
 */

/**
 * \addtogroup	EMBARC_APP_FREERTOS_IOT_AWS_SMARTHOME_NODES_LIVINGROOM
 * @{
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

#include <signal.h>
#include <sys/time.h>
#include <limits.h>

#include "os_hal_inc.h"

#include "aws_iot_config.h"
#include "aws_iot_log.h"
#include "aws_iot_version.h"
#include "aws_iot_mqtt_client_interface.h"
#include "aws_iot_shadow_interface.h"

#include "embARC.h"


#ifdef PATH_MAX
#undef PATH_MAX
#endif
#define PATH_MAX 256

#define MAX_LENGTH_OF_UPDATE_JSON_BUFFER 512

// initialize the mqtt client
AWS_IoT_Client mqttClient;
static char JsonDocumentBuffer[MAX_LENGTH_OF_UPDATE_JSON_BUFFER];
static size_t sizeOfJsonDocumentBuffer = sizeof(JsonDocumentBuffer) / sizeof(JsonDocumentBuffer[0]);

static bool LivingRoomLights = false;

static bool last_LivingRoomLights = false;

static bool LivingRoomLights_updated = false;

static jsonStruct_t LivingRoomLightsActuator;

static jsonStruct_t *curActuator[1];

#define CERT_ROOTDIR	"cert/livingRoom"

static char rootCA[PATH_MAX + 1];
static char clientCRT[PATH_MAX + 1];
static char clientKey[PATH_MAX + 1];

static char cafileName[] = AWS_IOT_ROOT_CA_FILENAME;
static char clientCRTName[] = AWS_IOT_CERTIFICATE_FILENAME;
static char clientKeyName[] = AWS_IOT_PRIVATE_KEY_FILENAME;

void ShadowUpdateStatusCallback(const char *pThingName, ShadowActions_t action, Shadow_Ack_Status_t status,
		const char *pReceivedJsonDocument, void *pContextData) {

	if (status == SHADOW_ACK_TIMEOUT) {
		IOT_INFO("Update Timeout--");
	} else if (status == SHADOW_ACK_REJECTED) {
		IOT_INFO("Update RejectedXX");
	} else if (status == SHADOW_ACK_ACCEPTED) {
		IOT_INFO("Update Accepted !!");
	}
}

#define LIVINGROOM_LEDMASK	0x3
static void controlLivingRoomLights(bool state)
{
	if (state) {
		IOT_INFO("Turn on LivingRoomLights");
		led_write(LIVINGROOM_LEDMASK, LIVINGROOM_LEDMASK);
	} else {
		IOT_INFO("Turn off LivingRoomLights");
		led_write(0x0, LIVINGROOM_LEDMASK);
	}
}

void LivingRoomLightsActuate_Callback(const char *pJsonString, uint32_t JsonStringDataLen, jsonStruct_t *pContext) {
	if (pContext != NULL) {
		bool temp = *(bool *)(pContext->pData);
		IOT_INFO("Delta - LivingRoomLights state changed to %d", temp);
		controlLivingRoomLights(LivingRoomLights);
	}
}

static void smarthome_init(void);
static void smarthome_close(void);
#define TIME_DELAY_UPDATE		(1)

#ifdef MID_NTSHELL
#define MAX_RETRY_TIMES			(20)
#else
#define MAX_RETRY_TIMES			(1000)
#endif

static void flash_leds(uint32_t delay_ms)
{
	for (int i = 0; i < 256; i ++) {
		led_write(i, 0xFF);
		vTaskDelay(delay_ms);
	}
}

static void smarthome_error(void)
{
#ifdef MID_NTSHELL
	led_write(0x1AA, BOARD_LED_MASK);
#else
	while (1) {
		flash_leds(100);
	}
#endif
}

static void smarthome_startup(void)
{
	IOT_INFO("++++Smarthome LivingRoom Node Startup++++");

#ifndef MID_NTSHELL
	flash_leds(15);
#else
	flash_leds(3);
#endif
	led_write(0x0, BOARD_LED_MASK);
}


#define NETWORK_LEDMASK		0x80
static void controlNetworkLed(bool state)
{
	if (state) {
		IOT_INFO("Network is ok");
		led_write(NETWORK_LEDMASK, NETWORK_LEDMASK);
	} else {
		IOT_INFO("Network is lost");
		led_write(0x0, NETWORK_LEDMASK);
	}
}

#define HEARTBEAT_LEDMASK	0x100
static bool heartbeat_state = true;
static void toggleHeartbeatLed(void)
{
	heartbeat_state = !heartbeat_state;
	if (heartbeat_state) {
		led_write(HEARTBEAT_LEDMASK, HEARTBEAT_LEDMASK);
	} else {
		led_write(0x0, HEARTBEAT_LEDMASK);
	}
}

static DEV_WNIC *p_wnic;
static void checkAndWaitNetwork(void)
{
	p_wnic = wnic_get_dev(BOARD_PMWIFI_0_ID);

	while (1) {
		if (p_wnic->wnic_info.conn_status == WNIC_CONNECTED) {
			controlNetworkLed(true);
			break;
		}
		controlNetworkLed(false);
		vTaskDelay(5000);
	}
}

void disconnectCallbackHandler(AWS_IoT_Client *pClient, void *data)
{
	IOT_WARN("MQTT Disconnect");
	IoT_Error_t rc = FAILURE;

	if (NULL == pClient) {
		return;
	}

	IOT_UNUSED(data);

	if (aws_iot_is_autoreconnect_enabled(pClient)) {
		IOT_INFO("Auto Reconnect is enabled, Reconnecting attempt will start now");
	} else {
		IOT_WARN("Auto Reconnect not enabled. Starting manual reconnect...");
		rc = aws_iot_mqtt_attempt_reconnect(pClient);
		if (NETWORK_RECONNECTED == rc) {
			IOT_WARN("Manual Reconnect Successful");
		} else {
			IOT_WARN("Manual Reconnect Failed - %d", rc);
		}
	}
}

int main(void)
{
	IoT_Error_t rc = SUCCESS;
	int32_t delay_ms = TIME_DELAY_UPDATE * 1000;
	int32_t retries = 0;

	smarthome_startup();

	LivingRoomLights_updated = false;

	LivingRoomLightsActuator.cb = LivingRoomLightsActuate_Callback;
	LivingRoomLightsActuator.pData = &LivingRoomLights;
	LivingRoomLightsActuator.pKey = "LightsOn";
	LivingRoomLightsActuator.type = SHADOW_JSON_BOOL;

	IOT_INFO("\nAWS IoT SDK Version(dev) %d.%d.%d-%s\n", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_TAG);

	sprintf(rootCA, "%s/%s", CERT_ROOTDIR, cafileName);
	sprintf(clientCRT, "%s/%s", CERT_ROOTDIR, clientCRTName);
	sprintf(clientKey, "%s/%s", CERT_ROOTDIR, clientKeyName);

	IOT_INFO("Using rootCA %s", rootCA);
	IOT_INFO("Using clientCRT %s", clientCRT);
	IOT_INFO("Using clientKey %s", clientKey);

	ShadowInitParameters_t sp = ShadowInitParametersDefault;
	ShadowConnectParameters_t scp = ShadowConnectParametersDefault;

	while (retries++ < MAX_RETRY_TIMES) {
		sp.pHost = AWS_IOT_MQTT_HOST;
		sp.port = AWS_IOT_MQTT_PORT;
		sp.pClientCRT = clientCRT;
		sp.pClientKey = clientKey;
		sp.pRootCA = rootCA;
		sp.enableAutoReconnect = true;
		sp.disconnectHandler = disconnectCallbackHandler;

		checkAndWaitNetwork();

		IOT_INFO("Shadow Init");
		rc = aws_iot_shadow_init(&mqttClient, &sp);
		if (SUCCESS != rc) {
			IOT_ERROR("Shadow initialize Error");
			return rc;
		}

		scp.pMyThingName = AWS_IOT_MY_THING_NAME;
		scp.pMqttClientId = AWS_IOT_MQTT_CLIENT_ID;
		scp.mqttClientIdLen = (uint16_t) strlen(AWS_IOT_MQTT_CLIENT_ID);

		IOT_INFO("Shadow Connect to AWS_IOT_MQTT_HOST: %s", sp.pHost);
		rc = aws_iot_shadow_connect(&mqttClient, &scp);

		if (SUCCESS != rc) {
			IOT_ERROR("Failed to connect to AWS_IOT_MQTT_HOST: %s", sp.pHost);
			IOT_ERROR("Shadow Connection Error %d", rc);
			IOT_INFO("\r\nPlease check the following things:")
			IOT_INFO("1. Whether the AWS_IOT_MQTT_HOST in aws_iot_config.h is changed according to your aws account.");
			IOT_INFO("2. Whether the rootCA, clientCRT and clientKey files are replaced according to your aws account.");
			IOT_INFO("3. Whether the modified rootCA(%s), clientCRT(%s) and clientKey(%s) files are copied to the right folder(%s) in your SDCard.",\
			 cafileName, clientCRTName, clientKeyName, CERT_ROOTDIR);
			IOT_INFO("4. If everything is modified and setup correctly, just wait for a while for connection retry.\r\n");
			vTaskDelay(1000);
			continue;
		} else {
			IOT_INFO("Shadow Connection OK");
		}

		/*
		 * Enable Auto Reconnect functionality. Minimum and Maximum time of Exponential backoff are set in aws_iot_config.h
		 *  #AWS_IOT_MQTT_MIN_RECONNECT_WAIT_INTERVAL
		 *  #AWS_IOT_MQTT_MAX_RECONNECT_WAIT_INTERVAL
		 */
		rc = aws_iot_shadow_set_autoreconnect_status(&mqttClient, true);
		if (SUCCESS != rc) {
			IOT_ERROR("Unable to set Auto Reconnect to true - %d", rc);
			return rc;
		}

		/** Register delta actuators */
		rc = aws_iot_shadow_register_delta(&mqttClient, &LivingRoomLightsActuator);

		if (SUCCESS != rc) {
			IOT_ERROR("Shadow Register Delta Error");
		} else {
			IOT_INFO("Shadow Register Delta OK");
		}

		smarthome_init();

		controlLivingRoomLights(LivingRoomLights);

		last_LivingRoomLights = !LivingRoomLights;

		uint32_t act_idx = 0;
		while (NETWORK_ATTEMPTING_RECONNECT == rc || NETWORK_RECONNECTED == rc || SUCCESS == rc) {
			rc = aws_iot_shadow_yield(&mqttClient, 200);
			if (NETWORK_ATTEMPTING_RECONNECT == rc) {
				vTaskDelay(1000);
				// If the client is attempting to reconnect we will skip the rest of the loop.
				continue;
			}
			/** Update desired messages */
			act_idx = 0;
			if (LivingRoomLights_updated) {
				curActuator[act_idx] = &LivingRoomLightsActuator;
				act_idx ++;
				LivingRoomLights = !LivingRoomLights;
				LivingRoomLights_updated = false;
				controlLivingRoomLights(LivingRoomLights);
			}

			if (act_idx > 0) {
				rc = aws_iot_shadow_init_json_document(JsonDocumentBuffer, sizeOfJsonDocumentBuffer);
				if (rc == SUCCESS) {
					if (act_idx == 1) {
						rc = aws_iot_shadow_add_desired(JsonDocumentBuffer, sizeOfJsonDocumentBuffer, 1, curActuator[0]);
					}
					if (rc == SUCCESS) {
						rc = aws_iot_finalize_json_document(JsonDocumentBuffer, sizeOfJsonDocumentBuffer);
						if (rc == SUCCESS) {
							IOT_INFO("Update Shadow Desired: %s", JsonDocumentBuffer);
							rc = aws_iot_shadow_update(&mqttClient, AWS_IOT_MY_THING_NAME, JsonDocumentBuffer, ShadowUpdateStatusCallback,
							NULL, 4, true);
						}
					}
				}
			}

			if (rc != SUCCESS) {
				continue;
			}
			/** Update reported messages */
			rc = aws_iot_shadow_init_json_document(JsonDocumentBuffer, sizeOfJsonDocumentBuffer);
			if (rc == SUCCESS) {
				rc = aws_iot_shadow_add_reported(JsonDocumentBuffer, sizeOfJsonDocumentBuffer, 1, &LivingRoomLightsActuator);
				if (rc == SUCCESS) {
					rc = aws_iot_finalize_json_document(JsonDocumentBuffer, sizeOfJsonDocumentBuffer);
					if (rc == SUCCESS) {
						IOT_INFO("Update Shadow: %s", JsonDocumentBuffer);
						rc = aws_iot_shadow_update(&mqttClient, AWS_IOT_MY_THING_NAME, JsonDocumentBuffer, ShadowUpdateStatusCallback,
						NULL, 4, true);
					}
				}
			}

			toggleHeartbeatLed();

			IOT_INFO("*****************************************************************************************\n");
			vTaskDelay(delay_ms);
		}

		smarthome_close();
		if (SUCCESS != rc) {
			IOT_ERROR("An error occurred in the loop %d", rc);
		}

		checkAndWaitNetwork();

		if (rc == FAILURE) {
			IOT_INFO("Disconnecting");
			rc = aws_iot_shadow_disconnect(&mqttClient);

			if (SUCCESS != rc) {
				IOT_ERROR("Disconnect error %d", rc);
			}
		}
	}
	smarthome_error();

	return rc;
}

/* interrupt service routine for LivingRoomLights control */
static void LivingRoomLights_button_isr(void *ptr)
{
	LivingRoomLights_updated = true;
}

#define BUTTON_USED_MASK	(0x2)

static void smarthome_init(void)
{
	DEV_GPIO_BIT_ISR bit_isr;
	DEV_GPIO_INT_CFG int_cfg;

	temp_sensor_init(TEMP_I2C_SLAVE_ADDRESS);

	DEV_GPIO_PTR port = gpio_get_dev(EMSK_BUTTON_PORT);

	port->gpio_control(GPIO_CMD_DIS_BIT_INT, (void *)BUTTON_USED_MASK);

	int_cfg.int_bit_mask = BUTTON_USED_MASK;
	int_cfg.int_bit_type = BUTTON_USED_MASK; /* edge trigger */
	int_cfg.int_bit_polarity = 0x0; /* falling sensitive */
	int_cfg.int_bit_debounce = BUTTON_USED_MASK; /* with debounce */

	port->gpio_control(GPIO_CMD_SET_BIT_INT_CFG, &int_cfg);

	/* LivingRoomLights button */
	bit_isr.int_bit_ofs = 1;
	bit_isr.int_bit_handler = LivingRoomLights_button_isr;
	port->gpio_control(GPIO_CMD_SET_BIT_ISR, &bit_isr);

	port->gpio_control(GPIO_CMD_ENA_BIT_INT, (void *)BUTTON_USED_MASK);

}

static void smarthome_close(void)
{
	DEV_GPIO_PTR port = gpio_get_dev(EMSK_BUTTON_PORT);
	port->gpio_control(GPIO_CMD_DIS_BIT_INT, (void *)BUTTON_USED_MASK);
}
/** @} */