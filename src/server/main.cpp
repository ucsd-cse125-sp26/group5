#include <iostream>

#include "shared/hello.h"
#include "shared/protocol.h"
#include <enet/enet.h>

#include <thread>
#include <chrono>
int main() {
  std::cout << "Hello World Server";
  shared::hello();

  if (enet_initialize () != 0)
    {
        fprintf (stderr, "An error occurred while initializing ENet.\n");
        return EXIT_FAILURE;
    }
    atexit (enet_deinitialize);

    ENetAddress address;
    ENetHost * server;
    
    /* Bind the server to the default localhost.     */
    /* A specific host address can be specified by   */
    /* enet_address_set_host (& address, "x.x.x.x"); */
    
    address.host = ENET_HOST_ANY;
    /* Bind the server to port 7777. */
    address.port = 7777;
    
    server = enet_host_create (& address /* the address to bind the server host to */, 
                                4      /* allow up to 32 clients and/or outgoing connections */,
                                  2      /* allow up to 2 channels to be used, 0 and 1 */,
                                  0      /* assume any amount of incoming bandwidth */,
                                  0      /* assume any amount of outgoing bandwidth */);
    if (server == NULL)
    {
        fprintf (stderr, 
                "An error occurred while trying to create an ENet server host.\n");
        exit (EXIT_FAILURE);
    }
    ENetEvent event;
    while (true) {
  
        while (enet_host_service (server, & event, 1000) > 0)
        {
            switch (event.type)
            {
              case ENET_EVENT_TYPE_CONNECT:
                printf ("A new client connected from %x:%u.\n", 
                        event.peer -> address.host,
                        event.peer -> address.port);
        
                /* Store any relevant client information here. */
                event.peer -> data = (void*) "Client information";
        
                break;
              case ENET_EVENT_TYPE_RECEIVE: {
                auto type = static_cast<shared::PacketType>(event.packet->data[0]);
                switch (type) {
                    case shared::PacketType::KEYBOARD_INPUT:
                      shared::InputPacket inputPacket;
                      std::memcpy(&inputPacket, event.packet->data, sizeof(shared::InputPacket));

                      printf("peer %x input: W=%d A=%d S=%d D=%d\n",
                      event.peer->address.host,
                      (inputPacket.keys & 0x01) != 0,
                      (inputPacket.keys & 0x02) != 0,
                      (inputPacket.keys & 0x04) != 0,
                      (inputPacket.keys & 0x08) != 0);
                      break;
                    default:
                      printf("Received unknown packet type %d\n", type);
                      break;
                }
                enet_packet_destroy (event.packet);
                break;
              }
              
              case ENET_EVENT_TYPE_DISCONNECT:
                  printf ("%s disconnected.\n", event.peer -> data);
          
                  /* Reset the peer's client information. */
          
                  event.peer -> data = NULL;
                  break;
              default:
                  break;

            }
        }
        // sleep for 50ms
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

    }
    enet_host_destroy(server);
    return EXIT_SUCCESS;


}
