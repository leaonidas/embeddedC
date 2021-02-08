/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 *
 */

#include "contiki.h"
#include "contiki-lib.h"
#include "contiki-net.h"
#include "net/ip/uip.h"
#include "net/rpl/rpl.h"
#include "simple-udp.h"

#include "net/netstack.h"
//#include "dev/button-sensor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#define DEBUG DEBUG_PRINT
#include "net/ip/uip-debug.h"

#define UIP_IP_BUF   ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])

#define UDP_CLIENT_PORT	8765
#define UDP_SERVER_PORT	5678
#define BROADCAST_PORT 10000
#define INFECTED_PORT 1234

#define UDP_EXAMPLE_ID  190

// size of the messages with the symptoms excluding '\0' character sent by the sensor node to the cluster node
#define MESSAGE_SYMPT_SIZE 5
// size of the message sent by the cluster and received by the sensor node: it contains a code ( explained below) and the state of the node
#define MESSAGE_STATE_SIZE 4
// BROADCAST_CODE represents when a message is delivered to all sensor nodes saying they became suspects of infection
// UNICAST_CODE represents the message a sensor node receives after sending its symptoms to the cluster and is waiting for the results
#define BROADCAST_CODE 1
#define UNICAST_CODE 2

static struct uip_udp_conn *server_conn;
static struct simple_udp_connection broadcast_connection, infected_connection;
//number of confirmed infected people
static int infected_number;

PROCESS(udp_server_process, "UDP server process");
AUTOSTART_PROCESSES(&udp_server_process);
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
//parses message received with the symptoms and store the values in the node structure
// we wanted to use sscanf to parse the msg but for some reason it's giving the warning "implicit declaration of function sscanf" but <stdio.h> is included. sprintf and then atoi results as well
static void interpret_msg(char *str, struct node *Node)
{
	char fever_str[2], cough_str[2], resp_str[2];	
	
	// the string received must be in the format: %d %d %d'\0', which means we must have 6 characters
	if(strlen(str) != MESSAGE_SYMPT_SIZE)
	{
		printf("Error receiving message: bad format\n");
		exit(1);
	}
	//parses the values
	sprintf(fever_str, "%c", str[0]);
	sprintf(cough_str, "%c", str[2]);
	sprintf(resp_str, "%c", str[4]);
	Node->fever = atoi(fever_str);
	Node->cough = atoi(cough_str);
	Node->resp= atoi(resp_str);
}
/*---------------------------------------------------------------------------*/
static void
receiver(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr,
         uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr,
         uint16_t receiver_port,
         const uint8_t *data,
         uint16_t datalen)
{
 // printf("Data received on port %d from port %d with length %d\n",
   //      receiver_port, sender_port, datalen);
}
/*---------------------------------------------------------------------------*/
static void
receiver_broadcast(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr,
         uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr,
         uint16_t receiver_port,
         const uint8_t *data,
         uint16_t datalen)
{
  printf("Data received on port %d from port %d with length %d\n",
         receiver_port, sender_port, datalen);
}
/*---------------------------------------------------------------------------*/
static void is_patient_infected(struct node *Node)
{
  int counter = 0, num=0;

	// init random generator
	//srand(clock_seconds());
	num = abs(rand() % 100);

  if(Node->fever==1) 
    counter++;
  if(Node->cough==1)
    counter++;
  if(Node->resp==1)
    counter++;
// 75% chance of being sick
  if(counter == 3)
  {
		//num = abs(rand() % 100);
    if(num >= 25)
      Node->state=2;
    else
      Node->state=0;    
  }
// 50% chance of being sick
  else if(counter == 2)
  {
    //num = abs(rand() % 100);
    if(num >= 50)
      Node->state=2;
    else
      Node->state=0;
  }
// 25% chance of being sick
  else if(counter == 1)
  {
    //num = abs(rand() % 100);
    if(num < 25)
      Node->state=2;
    else
      Node->state=0;
  }
// 10% chance of being sick
  else if(counter == 0)
  {
    //num=abs(rand() % 100);
    if(num < 10)
      Node->state=2;
    else
      Node->state=0;
  }
  printf("Num: %d inside sick\n", num);
}
/*---------------------------------------------------------------------------*/
//construct unicast message ( that's why the code is not passed as argument )
static void construct_msg(char *msg, int state)
{
	sprintf(msg, "2 %d",state);
}
/*---------------------------------------------------------------------------*/
static void msg_handler(uip_ipaddr_t addr)
{
  char *appdata;
	char reply_msg[MESSAGE_STATE_SIZE];
	//init reply_msg
	memset(reply_msg, '\0', sizeof(reply_msg));
	struct node Node;

	
  if(uip_newdata())
	{
		appdata = (char *)uip_appdata;
    appdata[uip_datalen()] = 0;
    printf("DATA recv '%s' from ", appdata);
    printf("%d",UIP_IP_BUF->srcipaddr.u8[sizeof(UIP_IP_BUF->srcipaddr.u8) - 1]);
    printf("\n");

		//interpret msg
		interpret_msg(appdata, &Node);
		//check if patient is infected
		is_patient_infected(&Node);
		//constructs message to send back to sensor node with code and Node state
		construct_msg(reply_msg, Node.state);

    printf("DATA sending reply\n");
		printf("reply_msg: %s\n",reply_msg);
    uip_ipaddr_copy(&server_conn->ripaddr, &UIP_IP_BUF->srcipaddr);
    uip_udp_packet_send(server_conn, reply_msg, strlen(reply_msg));
    // Restore server connection to allow data from any node 
    uip_create_unspecified(&server_conn->ripaddr);
		printf("MESSAGE SENT\n");
		
		// if patient is infected we need to notify all the sensor nodes from the cluster that they became suspects and increment the number of total infected people
		if(Node.state == 2)
		{		
			infected_number += 1;			
			printf("Sending broadcast to sensor nodes\n");
    	uip_create_linklocal_allnodes_mcast(&addr);
			// message "1 1" - the first one represents the broadcast code and the second one is the node state
    	simple_udp_sendto(&broadcast_connection, "1 1", 3, &addr);
		}
  }
}
/*---------------------------------------------------------------------------*/
static void
print_local_addresses(void)
{
  int i;
  uint8_t state;

  PRINTF("Server IPv6 addresses: ");
  for(i = 0; i < UIP_DS6_ADDR_NB; i++) {
    state = uip_ds6_if.addr_list[i].state;
    if(state == ADDR_TENTATIVE || state == ADDR_PREFERRED) {
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
PROCESS_THREAD(udp_server_process, ev, data)
{
  uip_ipaddr_t ipaddr;
  struct uip_ds6_addr *root_if;
	// this timer defines when we should send the informations about our countage of infected people to the data centers in our reach
  static struct etimer infected_interval;
  uip_ipaddr_t addr, addr_dc;
	//just used to manipulate numbers of infected people and send the message to datacenters
	static char infected_str[5];


  PROCESS_BEGIN();

	// connection to broadcast messages to sensor nodes
	simple_udp_register(&broadcast_connection, BROADCAST_PORT, NULL, BROADCAST_PORT, receiver_broadcast);
	// connection to broadcast messages to data center
	simple_udp_register(&infected_connection, INFECTED_PORT, NULL, INFECTED_PORT, receiver);

	// number of infections starts with zero
	infected_number = 0;

	//init clock module
	clock_init();
	srand(clock_seconds());

  PROCESS_PAUSE();

  //SENSORS_ACTIVATE(button_sensor);

  PRINTF("UDP server started\n");

#if UIP_CONF_ROUTER
/* The choice of server address determines its 6LoPAN header compression.
 * Obviously the choice made here must also be selected in udp-client.c.
 *
 * For correct Wireshark decoding using a sniffer, add the /64 prefix to the 6LowPAN protocol preferences,
 * e.g. set Context 0 to aaaa::.  At present Wireshark copies Context/128 and then overwrites it.
 * (Setting Context 0 to aaaa::1111:2222:3333:4444 will report a 16 bit compressed address of aaaa::1111:22ff:fe33:xxxx)
 * Note Wireshark's IPCMV6 checksum verification depends on the correct uncompressed addresses.
 */
 
#if 0
/* Mode 1 - 64 bits inline */
   uip_ip6addr(&ipaddr, 0xaaaa, 0, 0, 0, 0, 0, 0, 1);
#elif 1
/* Mode 2 - 16 bits inline */
  uip_ip6addr(&ipaddr, 0xaaaa, 0, 0, 0, 0, 0x00ff, 0xfe00, 1);
#else
/* Mode 3 - derived from link local (MAC) address */
  uip_ip6addr(&ipaddr, 0xaaaa, 0, 0, 0, 0, 0, 0, 0);
  uip_ds6_set_addr_iid(&ipaddr, &uip_lladdr);
#endif

  uip_ds6_addr_add(&ipaddr, 0, ADDR_MANUAL);
  root_if = uip_ds6_addr_lookup(&ipaddr);
  if(root_if != NULL) {
    rpl_dag_t *dag;
    dag = rpl_set_root(RPL_DEFAULT_INSTANCE,(uip_ip6addr_t *)&ipaddr);
    uip_ip6addr(&ipaddr, 0xaaaa, 0, 0, 0, 0, 0, 0, 0);
    rpl_set_prefix(dag, &ipaddr, 64);
    PRINTF("created a new RPL dag\n");
  } else {
    PRINTF("failed to create a new RPL DAG\n");
  }
#endif /* UIP_CONF_ROUTER */
  
  print_local_addresses();

  /* The data sink runs with a 100% duty cycle in order to ensure high 
     packet reception rates. */
  NETSTACK_MAC.off(1);

  server_conn = udp_new(NULL, UIP_HTONS(UDP_CLIENT_PORT), NULL);
  if(server_conn == NULL) {
    PRINTF("No UDP connection available, exiting the process!\n");
    PROCESS_EXIT();
  }
  udp_bind(server_conn, UIP_HTONS(UDP_SERVER_PORT));

  PRINTF("Created a server connection with remote address ");
  PRINT6ADDR(&server_conn->ripaddr);
  PRINTF(" local/remote port %u/%u\n", UIP_HTONS(server_conn->lport),
         UIP_HTONS(server_conn->rport));
	//sets timer between 40 and 60 seconds. It's not a fixed number to avoid the data center be overloaded with messages from several clusters at the same time
	etimer_set(&infected_interval, CLOCK_SECOND * (rand() % (60 - 40 + 1) + 40) );

  while(1)
	{
    PROCESS_YIELD();
    if(ev == tcpip_event)
		{
      msg_handler(addr);
    }
		else if( ev == PROCESS_EVENT_TIMER && data == &infected_interval)
		{
			printf("Sending broadcast to data centers\n");
    	uip_create_linklocal_allnodes_mcast(&addr_dc);
			// sending broadcast to data centers in reach
			sprintf(infected_str, "%d", infected_number);
    	simple_udp_sendto(&infected_connection, infected_str, strlen(infected_str), &addr_dc);
			etimer_reset(&infected_interval);
		}
  }

  PROCESS_END();
}
/*----------------------------------------------------------------	-----------*/
