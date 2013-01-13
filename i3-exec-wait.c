/*
 * i3-exec-wait
 *
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define SUBSCRIBE "i3-ipc\n\x00\x00\x00\x02\x00\x00\x00[\"window\"]"

/*
 * Print help.
 *
 */
void help() {
  fprintf(stderr, "Supply a command you wish to execute (and its arguments if needed).\nIf you need to wait for more than one window use -n <INT> as first two arguments.\nE.g. i3-exec-wait xterm -title \"Rc Shell\" -e rc\n     i3-exec-wait -n 4 gimp\n");
  exit(2);
}

/*
 * Print an error and bail.
 *
 */
void bail(char *msg) {
  fprintf(stderr, msg);
  exit(2);
}

/*
 * Print the system error and die.
 *
 */
void die(char *msg) {
  perror(msg);
  exit(2);
}

/*
 * The ad-hoc struct for i3-ipc message header.
 * Packing is very important here.
 *
 */
#pragma pack(1)
typedef struct {
  uint8_t magic[6];
  uint32_t len;
  uint32_t type;
} ipc_header;

/*
 * Receive message from i3-ipc.
 * Returns pointer to payload or NULL.
 *
 */
char *get_msg(int sock) {
  ipc_header head;
  char *payload;
  int len;

  if ((len = recv(sock, &head, sizeof(head), MSG_WAITALL)) < 0)
    die("recv");

  if (head.len > 0) {
    payload = malloc(head.len);
    if (recv(sock, payload, head.len, MSG_WAITALL) < 0)
      die("recv");
    return payload;
  } else
    return NULL;
}

int main(int argc, char *argv[]) {
  int nwin = 1;     // for how many windows we wait.
  int ncmd = 1;     // where do command argv starts.
  char path[1024];  // here will be socket path.
  struct sockaddr_un ipcaddr;  // i3-ipc addr.
  int sock;         // i3-ipc socket.

  /* Parse arguments and setup nwin and ncmd. */
  if (argc < 2)
    help();
  else {
    if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))
      help();
    else if (!strcmp(argv[1], "-n")) {
      if (argc < 4)
        bail("You have to specify number of windows and a command!\n");
      nwin = atoi(argv[2]);
      if (nwin < 1)
        bail("Number of windows has to be > 0!\n");
      ncmd = 3;
    }
  }

  /* Get the path to i3 socket, or die. */
  FILE *fp;

  if (!(fp = popen("/bin/sh -c \"i3 --get-socketpath\"", "r")))
    die("popen");

  fgets(path, sizeof(path), fp);
  if (strlen(path) == 0)
    bail("Could not get i3 socket path!\n");

  path[strlen(path) - 1] = 0;

  /* Subscribe to "window" events. */
  int len;
  char *payload;

  if ((sock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
    die("socket");

  ipcaddr.sun_family = AF_UNIX;
  strcpy(ipcaddr.sun_path, path);
  len = strlen(ipcaddr.sun_path) + sizeof(ipcaddr.sun_family);
  if (connect(sock, (struct sockaddr *)&ipcaddr, len) == -1)
    die("connect");

  // Remember about the -1 for size! Not null-terminated.
  if (send(sock, SUBSCRIBE, sizeof(SUBSCRIBE) - 1, 0) == -1)
    die("send");

  payload = get_msg(sock);

  if (strlen(payload) != 16)
    bail("Could not subscribe for the window event!\n");

  //printf("payload: '%s'\n", payload);
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
    //printf("payload: '%s'\n", payload);
    free(payload);
    nwin--;
  }

  /* Clean up and exit. */
  shutdown(sock, 2);
  exit(0);
}
