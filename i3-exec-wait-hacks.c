/*
 * i3-exec-wait
 * © 2013 Piotr S. Staszewski
 * For licensing purposes assume current i3 license.
 *
 * http://www.drbig.one.pl
 *
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <xcb/xcb.h>
#include <xcb/xcb_ewmh.h>

#include "yajl/yajl_tree.h"

#define SUBSCRIBE "i3-ipc\n\x00\x00\x00\x02\x00\x00\x00[\"window\"]"

#ifdef DEBUG
  #define dbg(code) code;
#else
  #define dbg(code)
#endif

/* Global variables for X server interaction */
xcb_connection_t *x_conn;
xcb_ewmh_connection_t e_conn;
xcb_screen_t *screen;
xcb_window_t window;
xcb_atom_t sync_atom;
xcb_client_message_event_t sync_msg;

/*
 * Print help.
 *
 */
void help() {
  fprintf(stderr, "Supply a command you wish to execute (and its arguments if needed).\n");
  fprintf(stderr, "If you need to wait for more than one window use -n <INT> as first two arguments.\n");
  fprintf(stderr, "E.g. i3-exec-wait xterm -title \"Rc Shell\" -e rc\n");
  fprintf(stderr, "     i3-exec-wait -n 4 gimp\n");
  exit(1);
}

/*
 * Print an error and exit.
 *
 */
void die(const char *msg) {
  if (errno) {
    perror(msg);
    exit(2);
  } else {
    fprintf(stderr, "ERROR: %s!\n", msg);
    exit(1);
  }
}

/*
 * Receive message from i3-ipc.
 * Returns pointer to payload or NULL.
 *
 */
char *get_msg(int sock) {
  // The i3-ipc message header, packing is very important.
  #pragma pack(1)
  struct ipc_header {
    uint8_t magic[6];
    uint32_t len;
    uint32_t type;
  } head;
  char *payload;
  int len;

  if ((len = recv(sock, &head, sizeof(head), MSG_WAITALL)) < 0)
    die("recv");

  if (head.len > 0) {
    // Allocate +1, get a free \0.
    if ((payload = calloc(head.len + 1, sizeof(uint8_t))) == NULL)
      die("calloc");
    if (recv(sock, payload, head.len, MSG_WAITALL) < 0)
      die("recv");
    return payload;
  } else
    return NULL;
}

/*
 * Get the window ID from the JSON response.
 * Returns a signed long integer.
 *
 */
long get_window_id(char *json) {
  const char *path[] = { "container", "window", (const char *) 0 };
  yajl_val node;
  char errbuf[1024];
  long window_id;

  if ((node = yajl_tree_parse((const char *) json, errbuf, sizeof(errbuf))) == NULL) {
    dbg(if (strlen(errbuf)) fprintf(stderr, "%s", errbuf));
    die("JSON response parse error");
  }

  yajl_val window = yajl_tree_get(node, path, yajl_t_number);
  if (window)
    // YAJL handles numbers as 64bit (long long), but we know
    // an X window ID will fit in long.
    window_id = (long) YAJL_GET_INTEGER(window);
  else
    die("Window node not found in the JSON response");
  
  yajl_tree_free(node);

  return window_id;
}

/*
 * Get mapping status of the window.
 * Returns a unsigned integer.
 *
 */
unsigned int get_window_mapping(long window_id) {
  xcb_get_window_attributes_reply_t *attr;
  unsigned int mapping;

  attr = xcb_get_window_attributes_reply(x_conn, xcb_get_window_attributes(
        x_conn, (xcb_window_t) window_id), NULL);

  if (!attr)
    die("Could not get window mapping state");

  mapping = attr->map_state;
  dbg(printf("mapping state: %u\n", mapping));

  free(attr);

  return mapping;
}

/* 
 * C implementation of sync_with_i3 from the testsuite.
 *
 */
void sync_with_i3() {
  int cookie = rand();

  sync_msg.data.data32[1] = (long) cookie;
  dbg(printf("generated cookie: %d\n", cookie));

  dbg(printf("sending sync message\n"));

  xcb_send_event(x_conn, 0, screen->root, XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT, (char *) &sync_msg);
  xcb_flush(x_conn);

  dbg(printf("entering event wait loop\n"));

  xcb_generic_event_t *event;
  while ((event = xcb_wait_for_event(x_conn))) {
    dbg(printf("got an event: %u\n", event->response_type));
    if (event->response_type == 161) {
      xcb_client_message_event_t *reply = (xcb_client_message_event_t *) event;
      dbg(printf("received window id: %ld\n", (long int) reply->data.data32[0]));
      dbg(printf("received cookie: %lu\n", (unsigned long) reply->data.data32[1]));
      break;
    }
    free(event);
  }
  free(event);
  
  dbg(printf("synced\n"));
}

/*
 * Wait for focus.
 * EWMH version via _NET_ACTIVE_WINDOW.
 *
 */
void wait_focus(long window_id) {
  xcb_window_t active;

  dbg(printf("Waiting for focus\n"));

  while (1) {
    if (!(xcb_ewmh_get_active_window_reply(&e_conn, xcb_ewmh_get_active_window(&e_conn, 0), &active, NULL)))
      die("Could not get active window via EWMH");
    if ((long) active == window_id) break;
    printf("waiting\n!");
    usleep(10);
  }

  dbg(printf("Got focus\n"));  
}

/*
 * Wait for focus.
 * Plain XCB version via input focus polling.
 *
 */
void wait_focus2(long window_id) {
  xcb_get_input_focus_reply_t *focus;

  dbg(printf("Waiting for focus\n"));

  while (1) {
    focus = xcb_get_input_focus_reply(x_conn, xcb_get_input_focus(x_conn), NULL);
    if (focus && ((long) focus->focus == window_id))
      break;
    printf("waiting focus\n");
    usleep(10);
  }

  dbg(printf("Got focus\n"));

  free(focus);
}

int main(int argc, char *argv[]) {
  int nwin = 1;                 // for how many windows we wait.
  int ncmd = 1;                 // where do command argv starts.
  struct sockaddr_un ipcaddr;   // i3-ipc addr.
  int sock;                     // i3-ipc socket.
  long window_id;               // reparented window id.

  /* Parse arguments and setup nwin and ncmd. */
  if (argc < 2)
    help();
  else {
    if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))
      help();
    else if (!strcmp(argv[1], "-n")) {
      if (argc < 4)
        die("You have to specify number of windows and a command");
      nwin = atoi(argv[2]);
      if (nwin < 1)
        die("Number of windows has to be > 0");
      ncmd = 3;
    }
  }

  /* X setup */
  if ((x_conn = xcb_connect(NULL, NULL)) == NULL)
    die("Could not connect to the X server");

  if (!(xcb_ewmh_init_atoms_replies(&e_conn, xcb_ewmh_init_atoms(x_conn, &e_conn), NULL)))
    die("Could not get EWMH connection");

  if (!(screen = xcb_setup_roots_iterator(xcb_get_setup(x_conn)).data))
    die("Could not get screen");

  xcb_intern_atom_reply_t *reply;
  if ((reply = xcb_intern_atom_reply(x_conn, xcb_intern_atom(x_conn, 0, strlen("I3_SYNC"), "I3_SYNC"), NULL)) == NULL)
    die("Could not get I3_SYNC atom");
  sync_atom = reply->atom;
  free(reply);

  window = xcb_generate_id(x_conn);
  dbg(printf("sync window id: %ld\n", (long int) window));
  uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
  uint32_t values[2] = { screen->white_pixel,
    XCB_EVENT_MASK_EXPOSURE       | XCB_EVENT_MASK_BUTTON_PRESS   |
    XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION |
    XCB_EVENT_MASK_ENTER_WINDOW   | XCB_EVENT_MASK_LEAVE_WINDOW   |
    XCB_EVENT_MASK_KEY_PRESS      | XCB_EVENT_MASK_KEY_RELEASE };
  xcb_create_window(x_conn, 0, window, screen->root, 0, 0, 150, 150, 10,
      XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, mask, values);
  //xcb_map_window(x_conn, window);
  xcb_flush(x_conn);

  sync_msg.response_type = XCB_CLIENT_MESSAGE;
  sync_msg.format = 32;
  sync_msg.sequence = 0;
  sync_msg.window = screen->root;
  sync_msg.type = sync_atom;
  sync_msg.data.data32[0] = (long) window;

  /* initialize PRNG */
  srand(time(NULL));

  /* Get the path to i3 socket, or die. */
  FILE *fp;

  if (!(fp = popen("/bin/sh -c \"i3 --get-socketpath\"", "r")))
    die("popen");

  fgets(ipcaddr.sun_path, sizeof(ipcaddr.sun_path), fp);
  pclose(fp);

  if (strlen(ipcaddr.sun_path) == 0)
    die("Could not get i3 socket path");

  ipcaddr.sun_path[strlen(ipcaddr.sun_path) - 1] = 0;

  /* Subscribe to "window" events. */
  int len;
  char *payload;

  if ((sock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
    die("socket");

  ipcaddr.sun_family = AF_UNIX;
  len = strlen(ipcaddr.sun_path) + sizeof(ipcaddr.sun_family);
  if (connect(sock, (struct sockaddr *)&ipcaddr, len) == -1)
    die("connect");

  // Remember about the -1 for size! Not null-terminated.
  if (send(sock, SUBSCRIBE, sizeof(SUBSCRIBE) - 1, 0) == -1)
    die("send");

  payload = get_msg(sock);

  if (payload == NULL || strlen(payload) != 16) {
    if (payload != NULL)
      free(payload);
    die("Could not subscribe for the window event");
  }

  //dbg(printf("payload: '%s'\n", payload));
  free(payload);

  /* Spawn the command. */
  pid_t pid = fork();
  if (pid < 0)
    die("fork");
  else if (pid == 0) {
    execvp(argv[ncmd], argv + ncmd);
    die("execvp");
  }

  /* Wait for nwin windows to get reparented. */
  while (nwin > 0) {
    payload = get_msg(sock);
    //dbg(printf("payload: '%s'\n", payload));

    window_id = get_window_id(payload);
    dbg(printf("window id: %ld\n", window_id));

    if (get_window_mapping(window_id) == 2)
      wait_focus(window_id);

    sync_with_i3();

    free(payload);
    nwin--;
  }

  /* Clean up and exit. */
  shutdown(sock, 2);

  xcb_destroy_window(x_conn, window);
  xcb_ewmh_connection_wipe(&e_conn);
  xcb_disconnect(x_conn);

  exit(0);
}
