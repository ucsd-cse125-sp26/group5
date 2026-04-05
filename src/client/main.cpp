// clang-format off
#include <glad/gl.h>
#include <GLFW/glfw3.h>
// clang-format on

#include <iostream>

#include <enet/enet.h>
#include "shared/hello.h"
#include "cstring"
#include "shared/protocol.h"

int main() {
  std::cout << "Hello World Client";
  shared::hello();

  GLFWwindow* window;

  if (enet_initialize () != 0)
  {
      fprintf (stderr, "An error occurred while initializing ENet.\n");
      return EXIT_FAILURE;
  }
  atexit (enet_deinitialize);

  ENetHost * client;
 
  client = enet_host_create (NULL /* create a client host */,
              1 /* only allow 1 outgoing connection */,
              2 /* allow up 2 channels to be used, 0 and 1 */,
              0 /* assume any amount of incoming bandwidth */,
              0 /* assume any amount of outgoing bandwidth */);
  
  if (client == NULL)
  {
      fprintf (stderr, 
              "An error occurred while trying to create an ENet client host.\n");
      exit (EXIT_FAILURE);
  }

  ENetAddress address;
  ENetEvent event;
  ENetPeer *peer;
  enet_address_set_host(&address, "localhost");
  address.port = 7777;
  peer = enet_host_connect (client, & address, 2, 0);    
 
  if (peer == NULL)
  {
    fprintf (stderr, 
              "No available peers for initiating an ENet connection.\n");
    exit (EXIT_FAILURE);
  }
  
  /* Wait up to 5 seconds for the connection attempt to succeed. */
  if (enet_host_service (client, & event, 5000) > 0 &&
      event.type == ENET_EVENT_TYPE_CONNECT)
  {
      puts ("Connection to localhost:7777 succeeded.");
  }
  else
  {
      /* Either the 5 seconds are up or a disconnect event was */
      /* received. Reset the peer in the event the 5 seconds   */
      /* had run out without any significant event.            */
      enet_peer_reset (peer);
      puts ("Connection to localhost:7777 failed.");
      enet_host_destroy(client);
      return EXIT_FAILURE;
  }

  /* Initialize the library */
  if (!glfwInit()) return -1;

  /* Create a windowed mode window and its OpenGL context */
  window = glfwCreateWindow(640, 480, "Hello World", NULL, NULL);
  if (!window) {
    glfwTerminate();
    return -1;
  }

  /* Make the window's context current */
  glfwMakeContextCurrent(window);

  // GLAD
  int version = gladLoadGL(glfwGetProcAddress);
  printf("GL %d.%d\n", GLAD_VERSION_MAJOR(version),
         GLAD_VERSION_MINOR(version));
  
  uint8_t previousKeys = 0;

  /* Loop until the user closes the window */
  while (!glfwWindowShouldClose(window)) {

    while (enet_host_service (client, & event, 0) > 0)
    {
        switch (event.type)
        {
        case ENET_EVENT_TYPE_RECEIVE:
            printf("Received packet of length %zu\n", event.packet->dataLength);
            /* Clean up the packet now that we're done using it. */
            enet_packet_destroy (event.packet);
            
            break;
          
        case ENET_EVENT_TYPE_DISCONNECT:
            printf ("%s disconnected.\n", event.peer -> data);
    
            /* Reset the peer's client information. */
    
            event.peer -> data = NULL;
            break;
        default:
            break;
        }
    }



    /* Render here */
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* Swap front and back buffers */
    glfwSwapBuffers(window);

    /* Poll for and process events */
    glfwPollEvents();

    uint8_t currentKeys = 0;
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
      currentKeys |= 0x01;
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
      currentKeys |= 0x02;
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
      currentKeys |= 0x04;
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
      currentKeys |= 0x08;
    }
    if (currentKeys != previousKeys) {
      printf("Sending packet with keys %d\n", currentKeys);
      previousKeys = currentKeys;
      shared::InputPacket inputPacket;
      inputPacket.type = shared::PacketType::KEYBOARD_INPUT;
      inputPacket.keys = currentKeys;
      ENetPacket * packet = enet_packet_create (&inputPacket, sizeof(shared::InputPacket), ENET_PACKET_FLAG_RELIABLE);
      enet_peer_send (peer, 0, packet);
    }
  }

  enet_peer_disconnect (peer, 0);
/* Allow up to 3 seconds for the disconnect to succeed
 * and drop any packets received packets.
 */
 bool disconnect = false;
 while (enet_host_service (client, & event, 3000) > 0)
 {
     switch (event.type)
     {
     case ENET_EVENT_TYPE_RECEIVE:
         enet_packet_destroy (event.packet);
         break;
  
     case ENET_EVENT_TYPE_DISCONNECT:
         puts ("Disconnection succeeded.");
         disconnect = true;
         break;
     default:
         break;
     }
     if (disconnect) {
         break;
     }
 }
 
  glfwTerminate();
  enet_host_destroy(client);
  return 0;
}
