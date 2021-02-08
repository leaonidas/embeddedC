#include "contiki.h"
#include "contiki-lib.h"
#include "contiki-net.h"

#include "lib/random.h"
#include "sys/ctimer.h"
#include "sys/etimer.h"
#include "net/ip/uip.h"
#include "net/ipv6/uip-ds6.h"
#include "net/ip/uip-debug.h"

#include "sys/node-id.h"

#include "simple-udp.h"
#include "servreg-hack.h"

#include "dev/button-sensor.h"
#include "dev/leds.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>


#include "net/ip/uip-udp-packet.h"
#ifdef WITH_COMPOWER
#include "powertrace.h"
#endif

#define UDP_CLIENT_PORT 8765
#define UDP_SERVER_PORT 5678
#define BROADCAST_PORT 10000

#define UDP_EXAMPLE_ID  190

#define DEBUG DEBUG_PRINT
#include "net/ip/uip-debug.h"

// size of the messages with the symptoms and '\0' sent by the sensor node to the cluster node
#define MESSAGE_SYMPT_SIZE 6
// size of the message sent by the cluster and received by the sensor node: it contains a code ( explained below) and the state of the node
#define MESSAGE_STATE_SIZE 4
// BROADCAST_CODE represents when a message is delivered to all sensor nodes saying they became suspects of infection
// UNICAST_CODE represents the message a sensor node receives after sending its symptoms to the cluster and is waiting for the results
#define BROADCAST_CODE 1
#define UNICAST_CODE 2


#define SEND_INTERVAL		(60 * CLOCK_SECOND)
#define SEND_TIME		(5*100 * CLOCK_SECOND)
#define QUARENTEEN_TIME         (24*14*10 * CLOCK_SECOND)



static struct uip_udp_conn *client_conn;
static struct simple_udp_connection broadcast_connection;
static uip_ipaddr_t server_ipaddr;
static struct node Node;
static struct msg_info msg;

static int broadcast_flag; // turns 1 when broadcast message has been received
static char broadcast_str[4]; // data received in broadcast message 

/*---------------------------------------------------------------------------*/
PROCESS(sensor_node, "Sensor node activity");
//PROCESS(udp_client_process, "UDP client process");
//AUTOSTART_PROCESSES(&sensor_node, &udp_client_process);
AUTOSTART_PROCESSES(&sensor_node);
/*---------------------------------------------------------------------------*/
//struct that holds the information received from the cluster node: code of the message and node state
struct msg_info
{
	int code;
	int state;
};
/*---------------------------------------------------------------------------*/
struct node
{
	int fever;
	int cough;
	int resp;
	int state;/*0-healty;1-susp;2-inf;3-imune*/
};
/*---------------------------------------------------------------------------*/
// parses the message checking for errors
static void interpret_msg(char *str)
{
	char code_str[2], state_str[2];

	sprintf(code_str, "%c", str[0]);
	sprintf(state_str, "%c", str[2]);
	
	//check if its broadcast or unicast
	if(str[0] != '1' && str[0] != '2')
	{
		printf("Error interpreting message: code is not correct!\n");
		return;
	}
	if(str[1] != ' ')
	{
		printf("Error intepreting message: bad format!\n");
		return;
	}
	if(atoi(state_str) > 3)
	{
		printf("Error intepreting message: state is not correct!\n");
		return;
	}
	msg.code = atoi(code_str);
	msg.state = atoi(state_str);

}
/*---------------------------------------------------------------------------*/
// handle the incoming broadcast message
static void broadcast_handle(void)
{
	interpret_msg(broadcast_str);
	// if the node is already imune or sick or suspect he can't be a suspect again.
	if(Node.state == 0)
	{
		//code of the message can only be 1 since it is a broadcast message, otherwise an error hapenned and the program will end
		if(msg.code != 1)
		{
			printf("An error occurred passing the message: code is not correct. The program will end!\n");
			exit(1);
		}
		//in case everything went smoothly
		Node.state = msg.state;
		if(Node.state == 1)
		{
			leds_off(LEDS_GREEN);
			leds_on(LEDS_YELLOW);
			leds_off(LEDS_RED);
			printf("I am a suspect of infection \n");
		}
	}
	//reset broadcast flag
	broadcast_flag = 0;
}
/*---------------------------------------------------------------------------*/
static void
receiver_broadcast(struct simple_udp_connection *c, const uip_ipaddr_t *sender_addr, uint16_t sender_port, const uip_ipaddr_t *receiver_addr, uint16_t receiver_port, const uint8_t *data, uint16_t datalen)
{
	
	broadcast_flag = 1;
	
	printf("Data received on port %d from port %d with length %d\n",
         receiver_port, sender_port, datalen);

	strncpy(broadcast_str,(char*) data,4);
	printf("Data received: %s\n",broadcast_str);
}
/*---------------------------------------------------------------------------*/
static void rcv_unicast_msg(void)
{
  char *str = NULL;
 
  if(uip_newdata())
	{
    str = uip_appdata;
    str[uip_datalen()] = '\0';
    printf("Response from the server: '%s'\n", str);
  }
	interpret_msg(str);
}
/*---------------------------------------------------------------------------*/
static void send_msg(void)
{
  char buf[MESSAGE_SYMPT_SIZE]; // the size already contemplates '\0' character

  //seq_id++;
  //printf("\n\nDATA send to %d 'Hello %d'\n\n", server_ipaddr.u8[sizeof(server_ipaddr.u8) - 1], seq_id);
  sprintf(buf,"%d %d %d", Node.fever, Node.cough, Node.resp);
	printf("message sent to cluster: %s\n", buf);
  uip_udp_packet_sendto(client_conn, buf, strlen(buf), &server_ipaddr, UIP_HTONS(UDP_SERVER_PORT));
}
/*---------------------------------------------------------------------------*/
static void
print_local_addresses(void)
{
  int i;
  uint8_t state;

  PRINTF("Client IPv6 addresses: ");
  for(i = 0; i < UIP_DS6_ADDR_NB; i++) {
    state = uip_ds6_if.addr_list[i].state;
    if(uip_ds6_if.addr_list[i].isused &&
       (state == ADDR_TENTATIVE || state == ADDR_PREFERRED)) {
      PRINT6ADDR(&uip_ds6_if.addr_list[i].ipaddr);
      PRINTF("\n");
      /* hack to make address "final" */
      if (state == ADDR_TENTATIVE) {
	uip_ds6_if.addr_list[i].state = ADDR_PREFERRED;
      }
    }
  }
}
/*---------------------------------------------------------------------------*/
static void set_global_address(void)
{
  uip_ipaddr_t ipaddr;

  uip_ip6addr(&ipaddr, 0xaaaa, 0, 0, 0, 0, 0, 0, 0);
  uip_ds6_set_addr_iid(&ipaddr, &uip_lladdr);
  uip_ds6_addr_add(&ipaddr, 0, ADDR_AUTOCONF);

/* The choice of server address determines its 6LoPAN header compression.
 * (Our address will be compressed Mode 3 since it is derived from our link-local address)
 * Obviously the choice made here must also be selected in udp-server.c.
 *
 * For correct Wireshark decoding using a sniffer, add the /64 prefix to the 6LowPAN protocol preferences,
 * e.g. set Context 0 to aaaa::.  At present Wireshark copies Context/128 and then overwrites it.
 * (Setting Context 0 to aaaa::1111:2222:3333:4444 will report a 16 bit compressed address of aaaa::1111:22ff:fe33:xxxx)
 *
 * Note the IPCMV6 checksum verification depends on the correct uncompressed addresses.
 */
 
#if 0
/* Mode 1 - 64 bits inline */
   uip_ip6addr(&server_ipaddr, 0xaaaa, 0, 0, 0, 0, 0, 0, 1);
#elif 1
/* Mode 2 - 16 bits inline */
  uip_ip6addr(&server_ipaddr, 0xaaaa, 0, 0, 0, 0, 0x00ff, 0xfe00, 1);
#else
/* Mode 3 - derived from server link-local (MAC) address */
  uip_ip6addr(&server_ipaddr, 0xaaaa, 0, 0, 0, 0x0250, 0xc2ff, 0xfea8, 0xcd1a); //redbee-econotag
#endif
}
/*---------------------------------------------------------------------------*/
static void check_symptoms(void)
{
  int num;

	// init random generator
	//srand(clock_seconds());
  num= abs(rand() % 100);

  printf("Num in symp: %d\n", num);
// 40% chance of having 0 symptoms
	if (num < 40)
	{
		Node.cough=0;
		Node.fever=0;
		Node.resp=0;
  }
// 30% chance of having 1 symptoms
	else if(num >= 40 && num < 70)
	{
    Node.cough=1;
		Node.fever=0;
		Node.resp=0;
	}
// 20% chance of having 2 symptoms
  else if(num >= 70 && num < 90)
  {
    Node.cough=1;
    Node.fever=1;
		Node.resp=0;
  }
// 10% chance of having 3 symptoms
  else if(num >= 90 && num < 100)
  {
    Node.cough=1;
    Node.fever=1;
    Node.resp=1;
  }

  printf("Symptoms, fever: %d, cough: %d, resp: %d\n", Node.fever, Node.cough, Node.resp);
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(sensor_node, ev, data)
{

  static struct etimer sick_timer, timeout, broadcast_timer;
  //static struct node Node;
	//static struct msg_info msg;
	// message received from the cluster that contains the code ( broadcast or unicast ) and the updated state of the node
	//static char msg_recv[MESSAGE_STATE_SIZE];
	//init string
	//memset(msg_recv, '\0', sizeof(msg_recv));


  
	


  PROCESS_BEGIN();

	//init flag. it turns 1 one a broadcast message has been received	
	broadcast_flag = 0;
	// init string that keeps the incoming broadcast message
	memset(broadcast_str, '\0', sizeof(data));

	//regist broadcast	
	simple_udp_register(&broadcast_connection, BROADCAST_PORT, NULL, BROADCAST_PORT, receiver_broadcast);

	// for unicast
	set_global_address();
  
  PRINTF("UDP client process started\n");

  print_local_addresses();

  /* new connection with remote host */
  client_conn = udp_new(NULL, UIP_HTONS(UDP_SERVER_PORT), NULL); 
  if(client_conn == NULL)
	{
    PRINTF("No UDP connection available, exiting the process!\n");
    PROCESS_EXIT();
  }
  udp_bind(client_conn, UIP_HTONS(UDP_CLIENT_PORT)); 

  PRINTF("Created a connection with the server ");
  PRINT6ADDR(&client_conn->ripaddr);
  PRINTF(" local/remote port %u/%u\n",
	UIP_HTONS(client_conn->lport), UIP_HTONS(client_conn->rport));


  Node.state=0;
	Node.cough=0;
	Node.fever=0;
	Node.resp=0;

	//init clock module
	clock_init();
	srand(clock_seconds());

	// enable button
  SENSORS_ACTIVATE(button_sensor);
	// turn led on
  leds_on(LEDS_GREEN);



  while(1)
  {

		// if a broadcast message was received indicating that this node become a suspect		
		if(broadcast_flag == 1)
			broadcast_handle();	
	
		// in case in the meanwhile we receive a broadcast message we need a timer, otherwise we would be blocked in the condition of pressing a button to see the broadcast message
		etimer_set(&broadcast_timer, CLOCK_SECOND*5);	
		
		PROCESS_WAIT_EVENT_UNTIL( (ev == sensors_event && data == &button_sensor) || (ev == PROCESS_EVENT_TIMER && data == &broadcast_timer));

		// if a broadcast message was received indicating that this node become a suspect		
		if(broadcast_flag == 1)
			broadcast_handle();	

		//button clicked to check symptoms		
		else if( ev == sensors_event && data == &button_sensor)
		{		
			// every time we click the button we check if we have symptoms, unless we are already imune
			if(Node.state != 3)
			{
				check_symptoms();
				//send symptomps to cluster to be analyzed
				send_msg();
				//wait for message from the server
				etimer_set(&timeout, 15 * CLOCK_SECOND);
				PROCESS_WAIT_EVENT_UNTIL(ev == tcpip_event || (ev == PROCESS_EVENT_TIMER && data == &timeout));
				//PROCESS_WAIT_EVENT_UNTIL(ev == tcpip_event);
				//PROCESS_YIELD();		
				// if the message was lost we go to the next iteration of the while loop		
				if(ev == PROCESS_EVENT_TIMER)
				{
					printf("TIMEOUT!\n");
					continue;
				}
				// message received
				else if(ev == tcpip_event)
				{
				//receive Node state updated and updated the value on the sensor node side
				rcv_unicast_msg();
				// update Node state with the value that came from cluster
				Node.state = msg.state;

			
				/*etimer_set(&broadcast_timer, CLOCK_SECOND*900); // takes approximatelly 16 seconds to pass the 14 days of sickness - 40000
			  //PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_TIMER && data == &sick_timer );
				PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&sick_timer));
				*/
				}

			}

			printf("After button press: %d\n", Node.state);

			//i am still healthy		
			if(Node.state == 0)
			{
			  //leds_toggle(LEDS_GREEN);
				leds_on(LEDS_GREEN);
				leds_off(LEDS_YELLOW);
				leds_off(LEDS_RED);
				printf("Healthy!\n");
			}// suspect. Note: in practice we will never enter in this condition because we can only change the state to suspect after a broadcast message, which is evaluated after the broadcast_flag condition
			else if(Node.state == 1)
			{
				//leds_toggle(LEDS_YELLOW);
				leds_off(LEDS_GREEN);
				leds_on(LEDS_YELLOW);
				leds_off(LEDS_RED);
				printf("I am a suspect of infection \n");
			} // infected
			else if(Node.state == 2)
			{
				//leds_toggle(LEDS_YELLOW);
			  //leds_toggle(LEDS_RED);	
				leds_off(LEDS_GREEN);
				leds_off(LEDS_YELLOW);
				leds_on(LEDS_RED);
				printf("I am sick! Gonna have to stay in bed for 14 days...\n");
				//etimer_reset(&sick_timer);
				etimer_set(&sick_timer, CLOCK_SECOND*10); // takes approximatelly 16 seconds to pass the 14 days of sickness
			  //PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_TIMER && data == &sick_timer );
				PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&sick_timer));
				Node.state=3;
			  //leds_toggle(LEDS_RED);
			  //leds_toggle(LEDS_GREEN);
				leds_on(LEDS_GREEN);
				leds_off(LEDS_YELLOW);
				leds_off(LEDS_RED);
			  printf("Patient state is now imune!\n");

			}

			printf("Patient state: %d\n", Node.state);
		} // end of condition of button pressed
		
  }

  PROCESS_END();

}
