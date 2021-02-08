/*
 * Copyright (c) 2011, Swedish Institute of Computer Science.
 * All rights reserved.
 *
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
#include "lib/random.h"
#include "sys/ctimer.h"
#include "sys/etimer.h"
#include "net/ip/uip.h"
#include "net/ipv6/uip-ds6.h"


#include "simple-udp.h"


#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define INFECTED_PORT 1234

// receive messages from cluster_heads
static struct simple_udp_connection infected_connection;
// array with information about cluster_heads infected people. it contains the ip of the incoming cluster heads and the number of infected people in that cluster
static struct array_clusters *array;
// array size
static int array_size;

static struct msg_info msg;

/*---------------------------------------------------------------------------*/
PROCESS(datacenter_process, "datacenter process");
AUTOSTART_PROCESSES(&datacenter_process);
/*---------------------------------------------------------------------------*/
// struct that holds the information in the array
struct array_clusters
{
	uip_ipaddr_t addr;
	int infected;
};
/*---------------------------------------------------------------------------*/
//struct that holds the information of the received msg
struct msg_info
{
	int flag;//flag that indicates that a message was received and needs to be handled
	uip_ipaddr_t addr; // ip of the sender
	int infected; // number of infected people 
};
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
	int i;
	char data_str[5];
  printf("Data received on port %d from port %d with length %d\n",
         receiver_port, sender_port, datalen);
	for(i = 0; i < 16; i++)
		printf("%02x ", sender_addr->u8[i]);

	printf("data: %s\n", (char*)data);
	msg.flag = 1;
	//strcpy(msg.addr, (char*) *sender_addr);
	 uip_ipaddr_copy(&(msg.addr), sender_addr);
	strcpy(data_str, (char*) data);
	msg.infected = atoi(data_str);
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(datacenter_process, ev, data)
{
	static struct etimer check_receiver;
	static int ip_found, i, j;

  PROCESS_BEGIN();
	//init array size
	array_size = 0;
	// init array
	array = NULL;
	//init ip_found
	ip_found = 0;


  // connection to receive messages from cluster_heads
	simple_udp_register(&infected_connection, INFECTED_PORT, NULL, INFECTED_PORT, receiver);

  while(1)
	{
	
		etimer_set(&check_receiver, CLOCK_SECOND*1);	
		// Every second we check for new incoming messages
		PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_TIMER && data == &check_receiver);

		if(msg.flag == 1)
		{
			//re-initialize flag			
			msg.flag = 0;
			// in case our array is still empty
			if(array == NULL)
			{
				array = (struct array_clusters*) malloc(sizeof(struct array_clusters));
				array_size += 1;
				//array[array_size -1].addr = msg.addr;
				//strcpy(array[array_size -1].addr, msg.addr);
				 uip_ipaddr_copy(&(array[array_size - 1].addr), &(msg.addr));
				array[array_size -1].infected = msg.infected;
				printf("First position in array created \n");
			}
			//traverse array to compare ip
			else
			{
				for( i = 0; i < array_size; i++)
				{
					//ip already exists in array, we only need to update infected people number					
					if(uip_ipaddr_cmp(&(array[i].addr), &(msg.addr)))
					{
						array[i].infected = msg.infected;
						ip_found = 1;
						printf("IP already exists, goona update infected number\n");
						break;
					}
				}
				//if ip was not found we need to allocate a new position in the array
				if(!ip_found)
				{
					array = (struct array_clusters*) realloc(array, sizeof(struct array_clusters));
					array_size +=1;
					//array[array_size -1].addr = msg.addr;
					//strcpy(array[array_size -1].addr, msg.addr);
				 uip_ipaddr_copy(&(array[array_size - 1].addr), &(msg.addr));
					array[array_size -1].infected = msg.infected;
					printf("New input\n");
				}
				//reset variable
				ip_found = 0;
			}

			for( j = 0; j < array_size; j++)
			{
				for(i = 0; i < 16; i++)
					printf("%02x ", array[j].addr.u8[i]);
				printf("array[%d].infected = %d\n", j, array[j].infected);
			}
			
		} 
	
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
