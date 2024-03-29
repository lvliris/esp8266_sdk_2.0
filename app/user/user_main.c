/*
 * ESPRSSIF MIT License
 *
 * add a line here for testing
 *
 * Copyright (c) 2015 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>
 *
 * Permission is hereby granted for use on ESPRESSIF SYSTEMS ESP8266 only, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "user_master.h"
#ifdef DEBUG1
#include "esp_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

//#include "lwip/multi-threads/sockets_mt.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "pthread.h"
#include "string.h"
#include "uart.h"
#include "gpio.h"
#include "udp.h"
#include "key.h"

//#include "user_config.h"
#include "user_iot_version.h"
#include "user_esp_platform.h"
#include "user_plug.h"

//#include "espressif/esp8266/ets_sys.h"
//#include "osapi.h"
//#include "mem.h"
//#include "espconn.h"
//#include "espressif/esp8266/gpio_register.h"

#define AP_SSID     "ZFZN_"
#define AP_PASSWORD "12345678"

#define DST_AP_SSID     "zhaofengkeji"//"lamost-701"//"@PHICOMM"//"TP-LINK_6D3E"//"USR-WIFI"//
#define DST_AP_PASSWORD "88888888"//"lamostee701"//"wifiwifi"//"12345678"//

#define UDP_STRING		"HF-A11ASSISTHREAD"

#define REMOTE_IP		"101.201.211.87"//"192.168.0.106"//"10.10.100.104"//

#define UDP_LOCAL_PORT  48899
#define SERVER_PORT     8899
#define REMOTE_PORT		8080
#define DATA_LEN        128
#define MAX_CONN		10

#define ORDER_LEN	32
#define ORDER_NUM	30
#define MODE_NUM	20
#define SPI_FLASH_SEC_SIZE  4096
#define SPI_FLASH_START		0x7B
#define ADDR_TABLE_START 	0x101

//#define DEBUG

typedef int32 SOCKET;
typedef struct __pthread_t {char __dummy;} *pthread_t;

//void scan_done(void *arg, STATUS status);
void TCPClient(void *pvParameters);
void UDPServer(void *pvParameters);
void TCPServer(void *pvParameters);
void UartProcess(void *pvParameters);
void WaitClient(void *pvParameters);
void RecvData(void *pvParameters);
void ProcessData(void *pvParameters);
void Sendorder(void *pvParameters);
void CheckOnline(void *pvParameters);

void wifi_handle_event_cb(System_Event_t *evt);
void convertaddr(uint8 *buff);
void updateaddr(uint8 *buff);
void StrToHex(uint8* Str, uint8* Hex, uint8 len);
void HexToStr(uint8* Hex, uint8* Str, uint8 len);
int UpdateModeOrder(uint8 modeidx, uint8 *order, uint8 len);
int DeleteModeOrder(uint8 modeidx, uint8 *order, uint8 len);
int DropMode(uint8 modeidx);
int DisableMode(uint8 modeidx, uint8* order);
int EnableMode(uint8 modeidx, uint8* order);
int ProcessSensor(uint8 *order, uint8 modeidx);

static SOCKET client_conn[MAX_CONN];
static SOCKET sta_socket;
static int client_num=0;
static uint8 ctrlid[9]={0};
char test_mode = 2;
uint8 modectl[MODE_NUM];
LOCAL xQueueHandle stopstasock=NULL;
static uint8 retrytimes = 0;
static bool offline[100]={false};
#endif


char test_mode = 2;

/******************************************************************************
 * FunctionName : user_rf_cal_sector_set
 * Description  : SDK just reversed 4 sectors, used for rf init data and paramters.
 *                We add this function to force users to set rf cal sector, since
 *                we don't know which sector is free in user's application.
 *                sector map for last several sectors : ABCCC
 *                A : rf cal
 *                B : rf init data
 *                C : sdk parameters
 * Parameters   : none
 * Returns      : rf cal sector
 *******************************************************************************/
uint32 user_rf_cal_sector_set(void) {
	flash_size_map size_map = system_get_flash_size_map();
	uint32 rf_cal_sec = 0;

	switch (size_map) {
	case FLASH_SIZE_4M_MAP_256_256:
		rf_cal_sec = 128 - 5;
		break;

	case FLASH_SIZE_8M_MAP_512_512:
		rf_cal_sec = 256 - 5;
		break;

	case FLASH_SIZE_16M_MAP_512_512:
	case FLASH_SIZE_16M_MAP_1024_1024:
		rf_cal_sec = 512 - 5;
		break;

	case FLASH_SIZE_32M_MAP_512_512:
	case FLASH_SIZE_32M_MAP_1024_1024:
		rf_cal_sec = 1024 - 5;
		break;

	default:
		rf_cal_sec = 0;
		break;
	}

	return rf_cal_sec;
}

xQueueHandle SendOrderReady = NULL;
xQueueHandle SceneOrderAmountLimit = NULL;
void SendOrderInit()
{
	const uint8 SceneOrderAmount = 1;

	if(SendOrderReady == NULL)
		SendOrderReady = xQueueCreate(1, 1);

	if(SceneOrderAmountLimit == NULL)
		SceneOrderAmountLimit = xQueueCreate(SceneOrderAmount, 1);
}

/******************************************************************************
 * FunctionName : user_init
 * Description  : entry of user application, init user function here
 * Parameters   : none
 * Returns      : none
 *******************************************************************************/
void user_init(void) {

	uart_init_new();

	wifi_set_event_handler_cb(wifi_handle_event_cb);

	led_init();

	user_info();
	
	SendOrderInit();

#if ESP_PLATFORM
    /*Initialization of the peripheral drivers*/
    /* Also check whether assigned ip addr by the router.If so, connect to ESP-server  */
    //user_esp_platform_init();
	user_plug_init();
	uint8 *buff=(uint8*)zalloc(64);
    spi_flash_read(0x7E * SPI_FLASH_SEC_SIZE, (uint32*)buff, 40);
    user_esp_platform_set_token((uint8*)buff);
    free(buff);
#endif

    wifi_config();

	xTaskCreate(UDPServer, "tsk2", 350, NULL, 2, NULL);
	xTaskCreate(TCPServer, "tsk3", 300, NULL, 2, NULL);
	xTaskCreate(UartProcess, "tsk4", 350, NULL, 3, NULL);
	xTaskCreate(TCPClientProcess, "task5", 512, NULL, 2, NULL);
	xTaskCreate(SendOrderTask, "task6", 400, NULL, 3, NULL);
	//xTaskCreate(CheckOnline, "tsk5", 256, NULL, 2, NULL);
	//xTaskCreate(TCPClient, "tsk1", 256, NULL, 2, NULL);
}

/*void scan_done(void *arg, STATUS status)
 {
 uint8 ssid[33];
 char temp[128];
 if (status == OK){
 struct bss_info *bss_link = (struct bss_info *)arg;
 while (bss_link != NULL)
 {
 memset(ssid, 0, 33);
 if (strlen(bss_link->ssid) <= 32)
 memcpy(ssid, bss_link->ssid, strlen(bss_link->ssid));
 else
 memcpy(ssid, bss_link->ssid, 32);
 printf("(%d,\"%s\",%d,\""MACSTR"\",%d)\r\n",bss_link->authmode, ssid, bss_link->rssi,MAC2STR(bss_link->bssid),bss_link->channel);
 bss_link = bss_link->next.stqe_next;
 }
 }
 else{
 printf("scan fail !!!\r\n");
 }
 }*/
#ifdef DEBUG1
void TCPClient(void *pvParameters){
	int ret;
	uint8 recvbytes;
	char *pbuf,*recv_buf,*p;
	struct sockaddr_in remote_ip;
	xTaskHandle ProDataHandle;

	bzero(&remote_ip, sizeof(remote_ip));
	remote_ip.sin_family = AF_INET;
	remote_ip.sin_addr.s_addr = inet_addr(REMOTE_IP);
	remote_ip.sin_len = sizeof(remote_ip);
	remote_ip.sin_port = htons(REMOTE_PORT);

	while(1){
		if(retrytimes++ > 20)
			system_restart();

		sta_socket = socket(PF_INET, SOCK_STREAM, 0);
		if(sta_socket == -1){
			close(sta_socket);
			vTaskDelay(1000 / portTICK_RATE_MS);
			continue;
#ifdef DEBUG
			printf("ESP8266 TCP client task > socket error\n");
#endif
		}
#ifdef DEBUG
		printf("ESP8266 TCP client task > socket %d success!\n",sta_socket);
#endif


		ret = connect(sta_socket, (struct sockaddr *)(&remote_ip), sizeof(struct sockaddr));
		if(0 != ret){
			close(sta_socket);
			vTaskDelay(1000 / portTICK_RATE_MS);
			continue;
#ifdef DEBUG
			printf("ESP8266 TCP client task > connect fail!\n");
#endif
		}
#ifdef DEBUG
		printf("ESP8266 TCP client task > connect ok!\n");
#endif

		xTaskCreate(ProcessData, "ProcessData", 1024, &sta_socket, 2, &ProDataHandle);
	while(1){
		if(!ctrlid[0])
		{
			printf("<00000000U00000000000FF>");
			vTaskDelay(1000 / portTICK_RATE_MS);
			continue;
		}
		bool state=false;
		portBASE_TYPE xStatus = xQueueReceive(stopstasock,&state,0);
		if(xStatus == pdPASS && state)
		{
			printf("receive stop sta_socket signal\n");
			break;
		}

		pbuf = (char*)zalloc(100);
		//printf("allocate pbuf success\n");
		sprintf(pbuf, "GET /zfzn02/servlet/ElectricOrderServlet?masterCode=%s HTTP/1.1\r\n", ctrlid);
		write(sta_socket, pbuf, strlen(pbuf));
		write(sta_socket, "Connection:keep-alive\r\n", strlen("Connection:keep-alive\r\n"));
		write(sta_socket, "User-Agent:lwip1.3.2\r\n", strlen("User-Agent:lwip1.3.2\r\n"));
		write(sta_socket, "Host:101.201.211.87:8080\r\n", strlen("Host:101.201.211.87:8080\r\n"));
		if(write(sta_socket, "\r\n", 2) < 0){
#ifdef DEBUG
			printf("ESP8266 TCP client task > send fail!\n");
#endif
			close(sta_socket);
//			free(pbuf);
//			vTaskDelay(2000 / portTICK_RATE_MS);
//			vTaskDelete(ProDataHandle);
			free(pbuf);
			break;
		}
		free(pbuf);
		vTaskDelay(2000 / portTICK_RATE_MS);
#ifdef DEBUG
		printf("ESP8266 TCP client task > send socket %d success!\n",sta_socket);
#endif

	}//send get order
	}
	vTaskDelete(NULL);
}

void UartProcess(void *pvParameters) {

	//printf("Welcome to send uart data task!\n");

	//send a order to get mastercode
	printf("<00000000U00000000000FF>");

	uint8 i,orderidx,update;
	uint8 order[ORDER_NUM][ORDER_LEN],buff[100]={0};
	int32 len;
	SOCKET client_sock;
	while (1) {
		update = 0;
		//printf("ready uart data:stringlen=%d\n",stringlen);
		if (stringlen) {
#ifdef DEBUG
			printf("send uart data:stringlen=%d\n",stringlen);
			if(rxbuf != NULL)
				printf(rxbuf);
#endif
			if(rxbuf[0] == '#' && rxbuf[9] == 'U'){
				memcpy(ctrlid, rxbuf+1, 8);
			}
			if(rxbuf[0] == '#' && strcspn(rxbuf,"Y")==13){
				updateaddr(rxbuf);
				update = 4;
			}
			if(rxbuf[0] == '<' && strcspn(rxbuf,">") > 24){
				update = 4;
				if(rxbuf[14] == 'O')
				{
					updateaddr(rxbuf);
					memset(rxbuf, 0, 100);
					stringlen = 0;
					continue;
				}
			}
			for(i=0; i < client_num; i++){
				client_sock = client_conn[i];
				if (client_sock && (rxbuf != NULL)) {
					//sendto(client_sock, rxbuf, stringlen, 0,(struct sockaddr * )&remote_addr, (socklen_t )len);
					send(client_sock, rxbuf, stringlen, 0);
				}
			}
			if(rxbuf[0] == '<' && sta_socket != -1){
				if(update)
					rxbuf[17] = '\0';
				else
					rxbuf[13] = '\0';//upload 10 bytes info may be not supported,upload 2 bytes anyway
				sprintf(buff,"POST /zfzn02/servlet/ElectricStateServlet?electricState=<%s%s> HTTP/1.1\r\n", ctrlid, rxbuf+1);
				write(sta_socket, buff, strlen(buff));
				//write(sta_socket, "POST /zfzn02/servlet/ElectricStateServlet?electricState=<AA00FF620200E58DZ200> HTTP/1.1\r\n",
					//	strlen("POST /zfzn02/servlet/ElectricStateServlet?electricState=<AA00FF620200E58DZ200> HTTP/1.1\r\n"));
				write(sta_socket, "Connection:keep-alive\r\n", strlen("Connection:keep-alive\r\n"));
				write(sta_socket, "User-Agent:lwip1.3.2\r\n", strlen("User-Agent:lwip1.3.2\r\n"));
				write(sta_socket, "Host:101.201.211.87:8080\r\n", strlen("Host:101.201.211.87:8080\r\n"));
				write(sta_socket, "\r\n", 2);
			}
			if(rxbuf[0] == '<' && rxbuf[2] == 'A'){//if the message comes from modectrl do the next procedure
				if(update)
					convertaddr(rxbuf);
				orderidx = atoi(rxbuf+10);
				xTaskCreate(Sendorder, "Sendorder", 512, &orderidx, 2, NULL);
			}
			if(rxbuf[0] == '<' && rxbuf[2] == 'D'){
				orderidx = atoi(rxbuf + 17);
				ProcessSensor(rxbuf, orderidx);
			}
			memset(rxbuf, 0, 100);
			stringlen = 0;
		}
		vTaskDelay(100 / portTICK_RATE_MS);
	}
	vTaskDelete(NULL);
}

void UDPServer(void *pvParameters) {
	LOCAL uint32 sock_fd;
	struct sockaddr_in server_addr, from;
	struct ip_info info;
	int ret, nNetTimeout;
	char *udp_msg = (char *) zalloc(DATA_LEN);
	uint8 *addr = (uint8 *) zalloc(4);
	uint8 opmode;
	socklen_t fromlen;
#ifdef DEBUG
	printf("Hello, welcome to UDPtask!\r\n");
#endif
	//wifi_station_scan(NULL,scan_done);
	//printf(rxbuf);


	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(UDP_LOCAL_PORT);
	server_addr.sin_len = sizeof(server_addr);

	do {
		sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
		if (sock_fd == -1) {
#ifdef DEBUG
			printf("ESP8266 UDP task > failed to create socket!\n");
#endif
			vTaskDelay(1000 / portTICK_RATE_MS);
		}
	} while (sock_fd == -1);
#ifdef DEBUG
	printf("ESP8266 UDP task > create socket OK!\n");
#endif


	do {
		ret = bind(sock_fd, (struct sockaddr * )&server_addr,
				sizeof(server_addr));
		if (ret != 0) {
#ifdef DEBUG
			printf("ESP8266 UDP task > captdns_task failed to bind socket\n");
#endif
			vTaskDelay(1000 / portTICK_RATE_MS);
		}
	} while (ret != 0);
#ifdef DEBUG
	printf("ESP8266 UDP task > bind OK!\n");
#endif


	while (1) {
		memset(udp_msg, 0, DATA_LEN);
		memset(&from, 0, sizeof(from));

		setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, (char * )&nNetTimeout,
				sizeof(int));
		fromlen = sizeof(struct sockaddr_in);
		ret = recvfrom(sock_fd, (uint8 * )udp_msg, DATA_LEN, 0,
				(struct sockaddr * )&from, (socklen_t* )&fromlen);
		if (ret > 0) {
#ifdef DEBUG
			printf("ESP8266 UDP task > recv %d Bytes from %s ,Port %d\n", ret, inet_ntoa(from.sin_addr), ntohs(from.sin_port));
#endif
			if (!strcmp(udp_msg, UDP_STRING)) {
				opmode = wifi_get_opmode();
				switch (opmode) {
				case SOFTAP_MODE:
					wifi_get_ip_info(0x01, &info);
					break;
				case STATION_MODE:
					if (wifi_station_get_connect_status() == STATION_GOT_IP)
						wifi_get_ip_info(0x00, &info);
					break;
				case STATIONAP_MODE:
					if (wifi_station_get_connect_status() == STATION_GOT_IP)
						wifi_get_ip_info(0x00, &info);
					else
						wifi_get_ip_info(0x01, &info);
					break;
				}
				if (&info != NULL) {
					addr = (uint8*) &(info.ip.addr);
					memset(udp_msg, 0, DATA_LEN);
					sprintf(udp_msg, "%d.%d.%d.%d,ACCF23635DAC,", addr[0],
							addr[1], addr[2], addr[3]);
#ifdef DEBUG
					printf("got ip addr!\n");
					printf("ip:%s\n",(uint8*)udp_msg);
					printf("stringlen=%d\n",stringlen);
#endif
					sendto(sock_fd, (uint8* )udp_msg, strlen(udp_msg), 0,
							(struct sockaddr * )&from, fromlen);
				}
			}
		}
	}

	if (udp_msg) {
		free(udp_msg);
		udp_msg = NULL;
	}
	close(sock_fd);

	vTaskDelete(NULL);
}

void TCPServer(void *pvParameters) {
	int32 listenfd;
	int32 ret;
	int32 client_sock;
	struct sockaddr_in server_addr;
	struct sockaddr_in remote_addr;
	int recbytes, stack_counter = 0;


	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_len = sizeof(server_addr);
	server_addr.sin_port = htons(SERVER_PORT);


	do {
		listenfd = socket(AF_INET, SOCK_STREAM, 0);
		if (listenfd == -1) {
#ifdef DEBUG
			printf("ESP8266 TCP server task > socket error\n");
#endif
			vTaskDelay(1000 / portTICK_RATE_MS);
		}
	} while (listenfd == -1);

#ifdef DEBUG
	printf("ESP8266 TCP server task > create socket: %d\n", listenfd);
#endif

	do {
		ret = bind(listenfd, (struct sockaddr * )&server_addr,
				sizeof(server_addr));
		if (ret != 0) {
#ifdef DEBUG
			printf("ESP8266 TCP server task > bind fail\n");
#endif
			vTaskDelay(1000 / portTICK_RATE_MS);
		}
	} while (ret != 0);
#ifdef DEBUG
	printf("ESP8266 TCP server task > port:%d\n",ntohs(server_addr.sin_port));
#endif

	do {

		ret = listen(listenfd, MAX_CONN);
		if (ret != 0) {
#ifdef DEBUG
			printf("ESP8266 TCP server task > failed to set listen queue!\n");
#endif
			vTaskDelay(1000 / portTICK_RATE_MS);
		}
	} while (ret != 0);
#ifdef DEBUG
	printf("ESP8266 TCP server task > listen ok:%d\n", listenfd);
#endif

	//pthread_t tpid;
	//FD_ZERO(&fdread);
	//struct listenfd_set *listen = (struct listenfd_set *)zalloc(sizeof(struct listenfd_set));
	//listen->listenfd = listenfd;
	//listen->fdread = &fdread;
	/* Waiting for TCP Client to connect */
	//xTaskCreate(WaitClient, "WaitClient", 256, NULL, 2, NULL);
	int32 len = sizeof(struct sockaddr_in);
	while (1) {
#ifdef DEBUG
		printf("ESP8266 TCP server task > wait client\n");
#endif

		if(client_num < MAX_CONN){
			/*block here waiting remote connect request*/
			if ((client_sock = accept(listenfd, (struct sockaddr * )&remote_addr,(socklen_t * )&len)) < 0) {
#ifdef DEBUG
				printf("ESP8266 TCP server task > accept fail\n");
#endif
				continue;
			}
#ifdef DEBUG
			printf("client num:%d\n",client_num);
			printf("ESP8266 TCP server task > Client from %s %d client_sock %d\n",inet_ntoa(remote_addr.sin_addr), htons(remote_addr.sin_port), client_sock);
#endif
			client_conn[client_num++] = client_sock;
			//FD_SET(client_sock, &fdread);
			xTaskCreate(RecvData, "RecvData", 256, &client_sock, 2, NULL);
		}


		/*char *recv_buf = (char *) zalloc(DATA_LEN);
		while ((recbytes = read(client_sock, recv_buf, DATA_LEN)) > 0) {
			recv_buf[recbytes] = 0;
#ifdef DEBUG
			printf("ESP8266 TCP server task > read data success %d!\nESP8266 TCP server task > ", recbytes);
			//sendto(client_sock, recv_buf, strlen(recv_buf), 0, (struct sockaddr *)&remote_addr, (socklen_t)len);
			send(client_sock,recv_buf,strlen(recv_buf),0);
#endif
			printf(recv_buf);
		}
		free(recv_buf);
		if (recbytes <= 0) {
#ifdef DEBUG
			printf("ESP8266 TCP server task > read data fail!\n");
#endif
			close(client_sock);
		}//*/
	}
	vTaskDelete(NULL);
}
/* Create a task to accept client, but the job finally done in TCPServer task */
void WaitClient(void *pvParameters){
	int ret;
	int32 len = sizeof(struct sockaddr_in);
	SOCKET cliconn,listenfd;
	struct sockaddr_in remote_addr;
#ifdef DEBUG
	printf("waiting for client...\nlistenfd:%d\n", listenfd);
#endif
	while(1){
		/*if((ret = select(0, fdread, NULL, NULL, NULL)) == -1){
			printf("select failed!ret:%d\n", ret);
			continue;
		}*/
		if(client_num < MAX_CONN){
#ifdef DEBUG
			printf("accepting...\n");
#endif
			cliconn = accept(listenfd, (struct sockaddr *)&remote_addr, (socklen_t *)&len );
			if(cliconn < 0){
				printf("accept failed!\n");
			}
			else{
				printf("accept ok!!!cliconn:%d,ip:%s,port:%d\n",cliconn, inet_ntoa(remote_addr.sin_addr),htons(remote_addr.sin_port));
				client_conn[client_num++] = cliconn;
				//printf("client num:%d\n",client_num);
				//FD_SET(cliconn, &fdread);
				xTaskCreate(RecvData, "RecvData", 256, &cliconn, 2, NULL);
			}
		}
		else{
			printf("connection full!\n");
		}
	}
	vTaskDelete(NULL);
}
/* Create a task to process the data of a single client */
void RecvData(void *pvParameters){
#ifdef DEBUG
	printf("reading data...\n");
#endif
	int ret,i,recvbytes;
	uint8 orderidx;
	//uint8 order[ORDER_NUM][ORDER_LEN],buff[ORDER_LEN];
	//fd_set *fdread = (fd_set *)fdread_t;
	SOCKET cliconn = *(SOCKET*)pvParameters;
	while(1){
		/*if((ret = select(0, fdread, NULL, NULL, NULL)) == -1){
			printf("select error!\n");
			continue;
		}*/
#ifdef DEBUG
		if(client_num)
			printf("client num:%d\n", client_num);
#endif
		//for(i=0; i < client_num; i++){
			//cliconn = client_conn[i];
#ifdef DEBUG
			printf("cliconn:%d\n",cliconn);
#endif
			if(cliconn){//FD_ISSET(cliconn, &fdread)
				char *recv_buf = (char *)zalloc(DATA_LEN);
				recvbytes = read(cliconn, recv_buf, DATA_LEN);
				if(recvbytes > 0){
					if(recvbytes > 20 && recv_buf[0]=='<')
					{
						recv_buf[recvbytes] = 0;
						if(recv_buf[1]=='<'){
							spi_flash_erase_sector(ADDR_TABLE_START + 3);
							spi_flash_write((ADDR_TABLE_START + 3) * SPI_FLASH_SEC_SIZE, (uint32*)recv_buf, 64);
							sprintf(recv_buf, "set wifi ok!\n");
							printf(recv_buf);
							send(cliconn, recv_buf, strlen(recv_buf), 0);
							system_restart();
						}
						else if(recv_buf[9] == 'T'){
							orderidx = atoi(recv_buf+13);
							if(recv_buf[10] == 'H'){
								//orderidx = atoi(recv_buf+13);//mode control order index is 13
								if(orderidx >= 0 && orderidx < MODE_NUM && !modectl[orderidx]){
									xTaskCreate(Sendorder, "Sendorder", 512, &orderidx, 2, NULL);
								}
							}
							if(recv_buf[10] == 'G'){//disable mode
								//orderidx=recv_buf[13]-'0';
								modectl[orderidx]=1;
							}
							if(recv_buf[10] == 'S'){//enable mode
								//orderidx=recv_buf[13]-'0';
								modectl[orderidx]=0;
							}
							if(recv_buf[10] == 'R'){
								//orderidx = recv_buf[13]-'0';
								if(orderidx >= 0 && orderidx < MODE_NUM )
									spi_flash_erase_sector(SPI_FLASH_START - orderidx);
							}
							if(recv_buf[10] == 'T'){//just for debug
								//orderidx = recv_buf[13]-'0';
								spi_flash_read((SPI_FLASH_START - orderidx) * SPI_FLASH_SEC_SIZE,(uint32*)recv_buf,128);
								recv_buf[127]=0;
								for(i=0;i<128;i++)
									printf("%c",recv_buf[i]);
							}
							if(recv_buf[10] == 'U'){//just for debug
								//orderidx = recv_buf[13]-'0';
								spi_flash_read((ADDR_TABLE_START + orderidx) * SPI_FLASH_SEC_SIZE,(uint32*)recv_buf,128);
								recv_buf[127]=0;
								for(i=0;i<128;i++)
									printf("%c",recv_buf[i]);
							}
						}
						else{
							if(strcspn(recv_buf,"X") == 13)
								convertaddr(recv_buf);
							printf(recv_buf);
						}

					}
#ifdef DEBUG
					printf("read %d bytes success:%s\n", recvbytes, recv_buf);
					//send(cliconn, recv_buf, strlen(recv_buf), 0);
#endif
				}
				else if(recvbytes == 0){
					printf("end of file\n");
					for(i=0; i < client_num; i++)
						if(cliconn == client_conn[i])
							break;
					if(i == client_num)
						printf("error:connection not found!\n");
					else if(i < client_num-1)
						for( ; i < client_num; i++)
							client_conn[i] = client_conn[i+1];
					closesocket(cliconn);
					client_num--;
					break;
				}
				else{
					printf("socket disconnected!\n");
					for(i=0; i < client_num; i++)
						if(cliconn == client_conn[i])
							break;
					if(i == client_num)
						printf("error:connection not found!\n");
					else if(i < client_num-1)
						for( ; i < client_num; i++)
							client_conn[i] = client_conn[i+1];
					closesocket(cliconn);
					client_num--;
					break;
				}
				free(recv_buf);
			}
			else{
				printf("connection error!\n");
				closesocket(cliconn);
				client_num--;
				for( ; i < client_num; i++)
					client_conn[i] = client_conn[i+1];
				break;
			}
		//}
	}
	vTaskDelete(NULL);
}
/* Create a task to process the data from remote server */
void ProcessData(void *pvParameters){
#ifdef DEBUG
	printf("processing data...\n");
#endif
	uint8 buff[50];
	int recvbytes;
	uint8 orderidx,i;
	uint8 *recv_buf,*p,orderlen;
	uint32 *flash;
	SOCKET cliconn = *(SOCKET*)pvParameters;
	while(1){
		recvbytes = 0;
		recv_buf = (char*)zalloc(320);
		memset(recv_buf, 0, 320);
		if((recvbytes = read(sta_socket, recv_buf, 320)) > 0){
#ifdef DEBUG
			//recv_buf[recvbytes] = 0;
			printf("ESP8266 TCP client task > recv data %d bytes!\nESP8266 TCP client task > %s\n", recvbytes, recv_buf);
#endif
			p = strchr(recv_buf, '{');
			if(p){
				recvbytes = strcspn(p, "}");
//				if(recvbytes < 90){
//					p[recvbytes+1] = 0;
//					printf(p);
//				}
				while(recvbytes > 18){
					retrytimes = 0;//connect to server and receive data success,reset retry times
					p = strchr(p+1, '<');
					if(p == NULL)
						break;
					if(strcspn(p,"XT") == 13)
						convertaddr(p);
					orderlen = strcspn(p, ">") + 1;
#ifdef DEBUG
							printf("orderlen=%d\n",orderlen);
#endif
					if(p[13] == 'S'){
						orderidx = atoi(p+17);
						if(p[14] == 'R')
						{
							DeleteModeOrder(orderidx, p, orderlen);
						}
						else
						{
							UpdateModeOrder(orderidx, p, orderlen);
						}
					}
					else if(p[9] == 'T'){
						if(p[10] == 'H'){
							orderidx = atoi(p+13);
							if(orderidx >= 0 && orderidx < MODE_NUM && !modectl[orderidx]){
								xTaskCreate(Sendorder, "Sendorder", 512, &orderidx, 2, NULL);
							}
						}
						if(p[10] == 'G'){//disable mode
							orderidx = atoi(p+13);
							DisableMode(orderidx, p);
						}
						if(p[10] == 'S'){//enable mode
							orderidx = atoi(p+13);
							EnableMode(orderidx, p);
						}
						if(p[10] == 'R'){
							orderidx = atoi(p+13);
							DropMode(orderidx);
						}
					}
					else if(p[9] == 'X'){
						memcpy(buff, p, orderlen + 1);
						buff[orderlen + 1]=0;
						printf(buff);
						vTaskDelay(500 / portTICK_RATE_MS);
					}
					recvbytes -= orderlen;
				}
			}
		}
		free(recv_buf);

		if(recvbytes <= 0){
#ifdef DEBUG
			printf("ESP8266 TCP client task > read data fail!\n");
			printf("recvbytes=%d\n",recvbytes);
#endif
			bool state=true;
			xQueueSend(stopstasock,&state,0);
			close(sta_socket);
			//printf("sta_socket:%d\n",sta_socket);
			break;
		}
	}
	vTaskDelete(NULL);
}
/* Create a task to send mode order */
void Sendorder(void *pvParameters){
	uint8 orderidx = *(uint8*)pvParameters;
	uint8 i,buff[ORDER_LEN-4];
	//printf("orderidx:%d\n", orderidx);

	if(orderidx >= MODE_NUM)
	{
		vTaskDelete(NULL);
		return;
	}

	int totalBufferLen = ORDER_LEN * ORDER_NUM;
	uint8 *orderbuffer = (uint8*)zalloc(totalBufferLen * sizeof(uint8));
	spi_flash_read((SPI_FLASH_START - orderidx) * SPI_FLASH_SEC_SIZE, (uint32*)orderbuffer, totalBufferLen);
	uint8 *head = orderbuffer;
	uint8 addrLen;

	while(*head == '<'){
		addrLen = strlen(head);
		convertaddr(head);
		head[addrLen - 4] = '\0';
		printf(head);
		head += addrLen + 1;
		vTaskDelay(500 / portTICK_RATE_MS);
	}
	free(orderbuffer);
	vTaskDelete(NULL);
}

/* Create a task to check whether the devices are online */
void CheckOnline(void *pvParameters){
	uint32 addr,info;
	uint8 addrnum,rpage,i,j;
	uint8 order[25];
	sprintf(order,"<0*00****XO**********FF>");
	while(1){
		spi_flash_read((ADDR_TABLE_START + 2) * SPI_FLASH_SEC_SIZE, &info, 4);
		addrnum = info & 0xff;
		if(addrnum > 100)
			addrnum = 0;
		//printf("addrnum:%d\n", addrnum);
		rpage = ((info >> 8) & 0xff) % 2;
		for(i=0; i<addrnum; i++){
			spi_flash_read((ADDR_TABLE_START + rpage) * SPI_FLASH_SEC_SIZE + 12 * i + 8, &addr, 4);
			memcpy(order+5, (uint8*)&addr, 4);
			if(!offline[i]){
				offline[i] = true;
				for(j=0; j<3; j++){
					if(offline[i]){
						printf(order);
						vTaskDelay(1000 / portTICK_RATE_MS);
					}
					else
						break;
				}
			}
		}
		vTaskDelay(15000 / portTICK_RATE_MS);
	}
	vTaskDelete(NULL);
}
/* wifi event handle function */
void wifi_handle_event_cb(System_Event_t *evt) {
	//printf("event %x\n", evt->event_id);
	switch (evt->event_id) {
	case EVENT_STAMODE_CONNECTED:
#ifdef DEBUG
		printf("connect to ssid %s, channel %d\n",
				evt->event_info.connected.ssid,
				evt->event_info.connected.channel);
#endif
		GPIO_OUTPUT(GPIO_Pin_4, 0);		//outout 0,turn on led
#if ESP_PLATFORM
		user_esp_platform_init();
#endif
		break;
	case EVENT_STAMODE_DISCONNECTED:
#ifdef DEBUG
		printf("disconnect from ssid %s, reason %d\n",
				evt->event_info.disconnected.ssid,
				evt->event_info.disconnected.reason);
#endif
		GPIO_OUTPUT(GPIO_Pin_4, 1);		//outout 1,turn off led
		break;
	case EVENT_STAMODE_AUTHMODE_CHANGE:
#ifdef DEBUG
		printf("mode: %d -> %d\n", evt->event_info.auth_change.old_mode,
				evt->event_info.auth_change.new_mode);
#endif
		break;
	case EVENT_STAMODE_GOT_IP:
#ifdef DEBUG
		printf("ip:" IPSTR ",mask:" IPSTR ",gw:" IPSTR,
				IP2STR(&evt->event_info.got_ip.ip),
				IP2STR(&evt->event_info.got_ip.mask),
				IP2STR(&evt->event_info.got_ip.gw));
		printf("\n");
#endif
		xTaskCreate(TCPClient, "tsk1", 256, NULL, 2, NULL);
		break;
	case EVENT_SOFTAPMODE_STACONNECTED:
#ifdef DEBUG
		printf("station: " MACSTR "join, AID = %d\n",
				MAC2STR(evt->event_info.sta_connected.mac),
				evt->event_info.sta_connected.aid);
#endif
		GPIO_OUTPUT(GPIO_Pin_4, 0);		//outout 0,turn on led
		break;
	case EVENT_SOFTAPMODE_STADISCONNECTED:
#ifdef DEBUG
		printf("station: " MACSTR "leave, AID = %d\n",
				MAC2STR(evt->event_info.sta_disconnected.mac),
				evt->event_info.sta_disconnected.aid);
#endif
		GPIO_OUTPUT(GPIO_Pin_4, 1);		//outout 1,turn off led
		break;
	default:
		break;
	}
}

void updateaddr(uint8 *buff){
	uint32 info[2],addr[3];//Extaddr+shortaddr
	uint8 addrnum,rpage,wpage,i,j,updated=0;
	spi_flash_read((ADDR_TABLE_START + 2) * SPI_FLASH_SEC_SIZE, info, 8);
	if(info[0] == 0xffffffff)//which page to read and the total number of address
		info[0] = 0;
	if(info[1] == 0xffffffff)//times the address have changed
		info[1] = 0;
	addrnum = info[0] & 0xff;
	if(addrnum > 200)
		addrnum = 0;
	rpage = ((info[0] >> 8) & 0xff) % 2;
	//printf("addrnum:%d,rpage:%d\n",addrnum,rpage);
	wpage = (rpage + 1)%2;
	for(i=0; i<addrnum; i++){
		spi_flash_read((ADDR_TABLE_START + rpage) * SPI_FLASH_SEC_SIZE + 12 * i, addr, 12);
		if( !strncmp((uint8*)addr, buff+5,8) ){
			offline[i] = false;
			if( strncmp((uint8*)&addr[2], buff+15, 4) )//extaddr is the same while short addr is not,update it
				updated = 1;
			break;
		}
	}
	if( (i<addrnum && updated) ||  i == addrnum){
		if(i==addrnum)
			addrnum++;
		spi_flash_erase_sector(ADDR_TABLE_START + wpage);
		for(j=0; j< addrnum; j++){
			spi_flash_read((ADDR_TABLE_START + rpage) * SPI_FLASH_SEC_SIZE + 12 * j, addr, 12);
			//printf("read extaddr:%c%c%c%c\n",*((uint8*)addr),*((uint8*)addr+1),*((uint8*)addr+2),*((uint8*)addr+3));
			if(j == i){
				strncpy((uint8*)&addr[0], buff+5, 8);
				strncpy((uint8*)&addr[2], buff+15, 4);
			}
			spi_flash_write((ADDR_TABLE_START + wpage) * SPI_FLASH_SEC_SIZE + 12 * j, addr, 12);
			//printf("write sucess\n");
		}
		info[1]++;
		info[0] = (uint16)wpage<<8 | addrnum;
		spi_flash_erase_sector(ADDR_TABLE_START + 2);
		spi_flash_write((ADDR_TABLE_START + 2) * SPI_FLASH_SEC_SIZE, info, 8);
	}
}

void convertaddr(uint8 *buff){
	//printf("converting address\n");
	uint32 info,addr[3];
	uint8 addrnum,rpage,i;
	uint8 recvbytes = strcspn(buff,">");
	spi_flash_read((ADDR_TABLE_START + 2) * SPI_FLASH_SEC_SIZE, &info, 4);
	if(info == 0xffffffff)//which page to read and the total number of address
		info = 0;
	addrnum = info & 0xff;
	rpage = ((info >> 8) && 0xff) % 2;
	//printf("addrnum:%d,rpage:%d\n",addrnum,rpage);
	for(i=0; i<addrnum; i++){
		spi_flash_read((ADDR_TABLE_START + rpage) * SPI_FLASH_SEC_SIZE + 12 * i, addr, 12);
		//printf("read extaddr:%c%c%c%c\n",*((uint8*)addr),*((uint8*)addr+1),*((uint8*)addr+2),*((uint8*)addr+3));
		if( !strncmp((uint8*)addr, buff+5,8) ){
			//printf(buff);
			//printf("\n");
			strncpy(buff+5, (uint8*)&addr[2], 4);
			strncpy(buff+9, buff+13, recvbytes-12);
			buff[recvbytes-3]=0;
			//printf(buff);
			//printf("\n");
			break;
		}
	}
}

void StrToHex(uint8* Str, uint8* Hex, uint8 len)
{
  uint8 h1,h2,s1,s2,i;
  for(i=0; i < len; i++)
  {
    h1 = Str[2*i];
    h2 = Str[2*i+1];
    s1 = h1-0x30;
    if(s1 > 9)
      s1-=7;
    s2 = h2-0x30;
    if(s2 > 9)
      s2-=7;
    Hex[i]=s1*16+s2;
  }
}

void HexToStr(uint8* Hex, uint8* Str, uint8 len)
{
  char	ddl,ddh;
  int i;

  for (i=0; i<len; i++)
  {
    ddh = 48 + Hex[i] / 16;
    ddl = 48 + Hex[i] % 16;
    if (ddh > 57)
      ddh = ddh + 7;
    if (ddl > 57)
      ddl = ddl + 7;
    Str[i*2] = ddh;
    Str[i*2+1] = ddl;
  }

  Str[len*2] = '\0';
}

int UpdateModeOrder(uint8 modeidx, uint8 *order, uint8 len)
{
	if( modeidx >= MODE_NUM )
		return -1;

	uint8 buff[ORDER_LEN+1] = { 0 };
	int totalBufferLen = ORDER_NUM * ORDER_LEN;
	uint8 *orderbuffer = (uint8 *)zalloc(totalBufferLen * sizeof(uint8));
	uint8 *head = NULL;
	uint8 *ctrl = NULL;
	uint8 *modeidxending = NULL;
	int addrLen;
	int hwcodeLen;
	int usedBufferLen;
	int remainingBufferLen;

	if(order[2] == 'A')
	{
		convertaddr(order);
		memcpy(buff, order, ORDER_LEN - 4);
		printf(buff);
		free(orderbuffer);
		return 1;
	}

	if(order[2] == 'D')
	{
		spi_flash_read((SPI_FLASH_START - MODE_NUM) * SPI_FLASH_SEC_SIZE, (uint32*)orderbuffer, totalBufferLen);
		head = orderbuffer;
		ctrl = strchr(order, 'S');
		if(ctrl == NULL)
		{
			free(orderbuffer);
			return 0;
		}
		addrLen = ctrl - order;
		while(*head == '<')
		{
			if(!memcmp(head, order, addrLen) && (order[14] - 'G') == (head[16] - '0'))
				break;
			else
				head += strlen(head) + 1;
		}
		usedBufferLen = head - orderbuffer;
		remainingBufferLen = totalBufferLen - usedBufferLen;
		if(remainingBufferLen > len)
		{
			order[addrLen] = 'X';//13
			order[addrLen + 3] = order[addrLen + 1] - 'G' + '0';//16 14
			order[addrLen + 1] = 'H';//14
			memcpy(head, order, len);
			head[len] = '\0';
		}
		else
			printf("\r\norder full\r\n");
		spi_flash_erase_sector(SPI_FLASH_START - MODE_NUM);
		spi_flash_write((SPI_FLASH_START - MODE_NUM) * SPI_FLASH_SEC_SIZE, (uint32*)orderbuffer, totalBufferLen);
		free(orderbuffer);
		return 1;
	}

	if(order[2] == '9')
	{
		spi_flash_read((SPI_FLASH_START - modeidx) * SPI_FLASH_SEC_SIZE, (uint32*)orderbuffer, totalBufferLen);
		head = orderbuffer;
		ctrl = strchr(order, 'S');
		modeidxending = strchr(order, '*');
		if(ctrl == NULL || modeidxending == NULL)
		{
			free(orderbuffer);
			return 0;
		}
		addrLen = ctrl -order;
		hwcodeLen = modeidxending - order;
		hwcodeLen = len - hwcodeLen - 2;
		while(*head == '<')
		{
			if(!memcmp(order, head, addrLen) && !memcmp(modeidxending + 1, head + addrLen + 4, hwcodeLen))
				break;
			else
				head += strlen(head) + 1;
		}
		usedBufferLen = head - orderbuffer;
		remainingBufferLen = totalBufferLen - usedBufferLen;
		if(remainingBufferLen > len)
		{
			order[addrLen] = 'X';
			memcpy(head, order, addrLen + 4);
			memcpy(head + addrLen + 4, modeidxending + 1, hwcodeLen + 1);
			head[addrLen + hwcodeLen + 5] = '\0';
		}
		else
			printf("\r\norder full\r\n");
		spi_flash_erase_sector(SPI_FLASH_START - modeidx);
		spi_flash_write((SPI_FLASH_START - modeidx) * SPI_FLASH_SEC_SIZE, (uint32*)orderbuffer, totalBufferLen);
		free(orderbuffer);
		return 1;
	}

	spi_flash_read((SPI_FLASH_START - modeidx) * SPI_FLASH_SEC_SIZE, (uint32*)orderbuffer, totalBufferLen);
	head = orderbuffer;
	ctrl = strchr(order, 'S');
	if(head == NULL || ctrl == NULL)
	{
		free(orderbuffer);
		return 0;
	}
	addrLen = ctrl - order;

	while(*head == '<')
	{
		if(!memcmp(head, order, addrLen) && !memcmp(head + addrLen + 2, order + addrLen + 2, 2))
		{
			break;
		}
		else
		{
			head += strlen(head) + 1;
		}
	}

	usedBufferLen = head - orderbuffer;
	remainingBufferLen = totalBufferLen - usedBufferLen;
	int i;
	if(remainingBufferLen > len)
	{
		order[addrLen] = 'X';
		head[len] = 0;
		//printf("\norderlen:%d\norder:%s\n", len, head);
		memcpy(head, order, len);
		//printf("after copy:%s\n", head);
	}
	else
	{
		printf("\r\norder full\r\n");
		free(orderbuffer);
		return -1;
	}

	spi_flash_erase_sector(SPI_FLASH_START - modeidx);
	spi_flash_write((SPI_FLASH_START - modeidx) * SPI_FLASH_SEC_SIZE, (uint32*)orderbuffer, totalBufferLen);
	free(orderbuffer);
	return 1;
}

int DeleteModeOrder(uint8 modeidx, uint8 *order, uint8 len)
{
	if( modeidx >= MODE_NUM)
		return -1;

	uint8 buff[ORDER_LEN+1];
	if(order[2] == 'A')
	{
		memcpy(buff, order, len);
		buff[len] = '\0';
		printf(buff);
		return 1;
	}

	int totalBufferLen = ORDER_NUM * ORDER_LEN;
	uint8 *orderbuffer = (uint8 *)zalloc(totalBufferLen * sizeof(uint8));
	spi_flash_read((SPI_FLASH_START - modeidx) * SPI_FLASH_SEC_SIZE, (uint32*)orderbuffer, totalBufferLen);

	uint8 *head = orderbuffer;
	uint8 *ctrl = strchr(order, 'S');
	if(ctrl == NULL)
	{
		free(orderbuffer);
		return 0;
	}

	uint8 addrLen = ctrl - order;
	uint8 *find = NULL;
	uint8 deletelen;
	uint8 copylen;

	while(*head == '<')
	{
		if(find != NULL)
		{
			copylen = strlen(head) + 1;
			//memcpy(find, head, copylen);
			memmove(find, head, copylen);
			memset(find + copylen, 0xFF, head - find);
			find += copylen;
			head += copylen;
		}
		else if(!memcmp(head, order, addrLen) && !memcmp(head + addrLen + 2, order + addrLen + 2, 2))
		{
			find = head;
			deletelen = strlen(head) + 1;
			memset(head, 0xFF, deletelen);
			head += deletelen;
		}
		else
			head += strlen(head) + 1;
	}
	spi_flash_erase_sector(SPI_FLASH_START - modeidx);
	spi_flash_write((SPI_FLASH_START - modeidx) * SPI_FLASH_SEC_SIZE, (uint32*)orderbuffer, totalBufferLen);
	free(orderbuffer);
	return 1;
}

int DropMode(uint8 modeidx)
{
	if( modeidx >= MODE_NUM)
			return -1;
	spi_flash_erase_sector(SPI_FLASH_START - modeidx);
	return 1;
}

int EnableMode(uint8 modeidx, uint8 *order)
{
	if( modeidx >= MODE_NUM)
		return -1;

	int totalBufferLen = ORDER_NUM * ORDER_LEN;
	uint8 *orderbuffer = (uint8 *)zalloc(totalBufferLen * sizeof(uint8));
	uint8 *head = NULL;
	uint8 *ctrl = NULL;
	int addrLen;

	if(order[2] == 'D')
	{
		spi_flash_read((SPI_FLASH_START - MODE_NUM) * SPI_FLASH_SEC_SIZE, (uint32*)orderbuffer, totalBufferLen);
		head = orderbuffer;
		ctrl = strchr(order, 'T');
		addrLen = ctrl - head;
		while(*head == '<')
		{
			if(!memcmp(order, head, addrLen))
			{
				order[10] = 'H';
				memcpy(head, order, addrLen + 2);
				printf("enable mode OK!\n");
				break;
			}
			else
				head += strlen(head) + 1;
		}
		spi_flash_erase_sector(SPI_FLASH_START - MODE_NUM);
		spi_flash_write((SPI_FLASH_START - MODE_NUM) * SPI_FLASH_SEC_SIZE, (uint32*)orderbuffer, totalBufferLen);
		free(orderbuffer);
		return 1;
	}
	else
	{
		modectl[modeidx] = 0;
		return 1;
	}
}

int DisableMode(uint8 modeidx, uint8 *order)
{
	if( modeidx >= MODE_NUM)
		return -1;

	int totalBufferLen = ORDER_NUM * ORDER_LEN;
	uint8 *orderbuffer = (uint8 *)zalloc(totalBufferLen * sizeof(uint8));
	uint8 *head = NULL;
	uint8 *ctrl = NULL;
	int addrLen;

	if(order[2] == 'D')
	{
		spi_flash_read((SPI_FLASH_START - MODE_NUM) * SPI_FLASH_SEC_SIZE, (uint32*)orderbuffer, totalBufferLen);
		head = orderbuffer;
		ctrl = strchr(order, 'T');
		addrLen = ctrl - head;
		while(*head == '<')
		{
			if(!memcmp(order, head, addrLen))
			{
				order[10] = 'G';
				memcpy(head, order, addrLen + 2);
				printf("disable mode OK!\n");
				break;
			}
			else
				head += strlen(head) + 1;
		}
		spi_flash_erase_sector(SPI_FLASH_START - MODE_NUM);
		spi_flash_write((SPI_FLASH_START - MODE_NUM) * SPI_FLASH_SEC_SIZE, (uint32*)orderbuffer, totalBufferLen);
		free(orderbuffer);
		return 1;
	}
	else
	{
		modectl[modeidx] = 1;
		return 1;
	}
}

int ProcessSensor(uint8 *order, uint8 modeidx)
{
	int totalBufferLen = ORDER_NUM * ORDER_LEN;
	uint8 orderidxs;
	uint8 *orderbuffer = (uint8*)zalloc(totalBufferLen * sizeof(uint8));
	spi_flash_read((SPI_FLASH_START - MODE_NUM) * SPI_FLASH_SEC_SIZE, (uint32*)orderbuffer, totalBufferLen);
	uint8 *head = orderbuffer;
	while(*head == '<')
	{
		if(!memcmp(head, order, 13) && ((order[16] - '0') & 1) == (head[16] - '0') && head[14] == 'H')
		{
#ifdef DEBUG
		printf(head);
		printf("\nreceived flag:%d, saved flag:%d\n",((order[16]-'0')&1), (head[16]-'0'));
#endif
			modeidx = atoi(head + 17);
			//printf("ProcessSensor orderidx:%d\n", modeidx);
			xTaskCreate(Sendorder, "Sendorder", 512, &modeidx, 2, NULL);
			//delay_ms(500);
			break;
		}
		else
		{
			head += strlen(head) + 1;
		}
	}
	free(orderbuffer);
	return 1;

}
#endif























