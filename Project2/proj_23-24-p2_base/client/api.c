#include "api.h"
#include "main.h"
#include "common/constants.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

int sv_fd;
int fd_req;
int fd_resp;
int session_id;

int ems_setup(char const* req_pipe_path, char const* resp_pipe_path, char const* server_pipe_path) {
  unlink(req_pipe_path);
  unlink(resp_pipe_path);

  // 0640 -> if constants not working
  if (mkfifo(req_pipe_path, S_IRUSR | S_IWUSR | S_IRGRP) != 0) {
    fprintf(stderr, "Creation of request pipe failed in directory %s.\n", req_pipe_path);
    return 1;
  }

  if (mkfifo(resp_pipe_path, S_IRUSR | S_IWUSR | S_IRGRP) != 0) {
    fprintf(stderr, "Creation of response pipe failed in directory %s.\n", resp_pipe_path);
    return 1;
  }

  sv_fd = open(server_pipe_path, O_WRONLY);

  // Request msgs
  char OP_CODE = EMS_SETUP_CODE;
  if (write(sv_fd, &OP_CODE, sizeof(char)) == -1) {
    fprintf(stderr, "Failed to write the OP_CODE on the server pipe.\n");
    return 1;
  }
  if (write(sv_fd, req_pipe_path, sizeof(char) * MAX_PIPENAME_SIZE) == -1) {
    fprintf(stderr, "Failed to write the request pipe path on the server pipe.\n");
    return 1;
  }
  if (write(sv_fd, resp_pipe_path, sizeof(char) * MAX_PIPENAME_SIZE) == -1) {
    fprintf(stderr, "Failed to write the response pipe path on the server pipe.\n");
    return 1;
  }

  fprintf(stderr, "Opening request pipe...\n");
  fd_req = open(req_pipe_path, O_WRONLY);
  if (fd_req == -1) {
    fprintf(stderr, "Failed opening request pipe.\n");
    exit(EXIT_FAILURE);
  }
  fprintf(stderr, "Opened request pipe!\n");

  fprintf(stderr, "Opening response pipe...\n");
  fd_resp = open(resp_pipe_path, O_RDONLY);
  if (fd_resp == -1) {
    fprintf(stderr, "Failed opening response pipe.\n");
    exit(EXIT_FAILURE);
  }
  fprintf(stderr, "Opened response pipe!\n");

  // Response msgs
  if (read(fd_resp, &session_id, sizeof(int)) == -1) {
    fprintf(stderr, "Failed to read this client session id from the server pipe.\n");
    return 1;
  }

  return 0;
}

int ems_quit(void) {
  fprintf(stderr, "We ended here!");
  char OP_CODE = EMS_QUIT_CODE;
  if (write(fd_req, &OP_CODE, sizeof(char)) == -1) {
    fprintf(stderr, "Failed to write the OP_CODE on the request pipe.\n");
    return 1;
  }

  close(fd_req);
  close(fd_resp);
  return 1; 
}

int ems_create(unsigned int event_id, size_t num_rows, size_t num_cols) {

  // Request pipe
  char OP_CODE = EMS_CREATE_CODE;
  if (write(fd_req, &OP_CODE, sizeof(char)) == -1) {
    fprintf(stderr, "Failed to write the OP_CODE on the request pipe.\n");
    return 1;
  }
  if (write(fd_req, &event_id, sizeof(unsigned int)) == -1) {
    fprintf(stderr, "Failed to write the event ID on the request pipe.\n");
    return 1;
  }
  if (write(fd_req, &num_rows, sizeof(size_t)) == -1) {
    fprintf(stderr, "Failed to write the number of rows on the request pipe.\n");
    return 1;
  }
  if (write(fd_req, &num_cols, sizeof(size_t)) == -1) {
    fprintf(stderr, "Failed to write the number of columns on the request pipe.\n");
    return 1;
  }

  // Response pipe
  int return_value;
  if (read(fd_resp, &return_value, sizeof(int)) == -1) {
    fprintf(stderr, "Failed to read response sent by server.\n");
    return 1;
  }

  if (return_value == SUCCESS_MSG) {
    // done
  }
  else {
    fprintf(stderr, "Failed to create an event on client %d, with error value %d.\n", session_id, return_value);
    return 1;
  }
 
  return 0;
}

int ems_reserve(unsigned int event_id, size_t num_seats, size_t* xs, size_t* ys) {

  char OP_CODE = EMS_RESERVE_CODE;
  if (write(fd_req, &OP_CODE, sizeof(char)) == -1) {
    fprintf(stderr, "Failed to write the OP_CODE on the request pipe.\n");
    return 1;
  }
  if (write(fd_req, &event_id, sizeof(unsigned int)) == -1) {
    fprintf(stderr, "Failed to write the event ID on the request pipe.\n");
    return 1;
  }
  if (write(fd_req, &num_seats, sizeof(size_t)) == -1) {
    fprintf(stderr, "Failed to write the number of seats on the request pipe.\n");
    return 1;
  }
  if (write(fd_req, xs, sizeof(size_t) * num_seats) == -1) {
    fprintf(stderr, "Failed to write the xs value on the request pipe.\n");
    return 1;
  }
  if (write(fd_req, ys, sizeof(size_t) * num_seats) == -1) {
    fprintf(stderr, "Failed to write the ys value on the request pipe.\n");
    return 1;
  }

  // Response pipe
  int return_value;
  if (read(fd_resp, &return_value, sizeof(int)) == -1) {
    fprintf(stderr, "Failed to read response sent by server.\n");
    return 1;
  }

  printf("Reponse -> %d\n", return_value);
  if (return_value == SUCCESS_MSG) {
    // done
  }
  else {
    fprintf(stderr, "Failed to reserve a seat on an event on client %d.\n", session_id);
    return 1;
  }

  return 0;
}

int ems_show(int out_fd, unsigned int event_id) {
  //TODO: send show request to the server (through the request pipe) and wait for the response (through the response pipe)
  char OP_CODE = EMS_SHOW_CODE;
  if (write(fd_req, &OP_CODE, sizeof(char)) == -1) {
    fprintf(stderr, "Failed to write the OP_CODE on the request pipe.\n");
    return 1;
  }
  if (write(fd_req, &event_id, sizeof(unsigned int)) == -1) {
    fprintf(stderr, "Failed to write the event ID on the request pipe.\n");
    return 1;
  }

  // Response pipe
  int return_value;
  if (read(fd_resp, &return_value, sizeof(int)) == -1) {
    
    fprintf(stderr, "Failed to read response sent by server.\n");
    return 1;
  }


  size_t num_rows, num_cols;
  unsigned int* seats; 
  
  if (return_value == SUCCESS_MSG) {
    if (read(fd_resp, &num_rows, sizeof(size_t)) == -1) {
      fprintf(stderr, "Failed to read the number of rows from the response pipe.\n");
      return 1;
    }
    if (read(fd_resp, &num_cols, sizeof(size_t)) == -1) {
      fprintf(stderr, "Failed to read the number of cols from the response pipe.\n");
      return 1;
    }
    seats = (unsigned int*) malloc(sizeof(unsigned int) * (num_rows * num_cols));
    
    if (read(fd_resp, seats, sizeof(unsigned int) * (num_rows * num_cols)) == -1) {
      fprintf(stderr, "Failed to read the seats information from the response pipe.\n");
      return 1;
    }

    fprintf(stderr, "fd -> %d.\n", out_fd);
  }
  else {
    fprintf(stderr, "Failed to show an event on client %d.\n", session_id);
    return 1;
  }

  char end_line = '\n';
  char space = ' ';
  int counter = 1;

  for (int i = 0; i < (int)( num_rows * num_cols); i++) {
    char test = (char)(seats[i] + '0');


    if(write(out_fd, &test, sizeof(test)) == -1) {
      fprintf(stderr, "Failed to write event in .out file.\n");
    }

    if (counter == (int) num_cols) {
      if(write(out_fd, &end_line, sizeof(end_line)) == -1) {
        fprintf(stderr, "Failed to write event in .out file.\n");
      }
      counter = 1;
    } else {
      if(write(out_fd, &space, sizeof(space)) == -1) {
        fprintf(stderr, "Failed to write event in .out file.\n");
      }
      counter ++;
    }
  }

  return 0;
}

int ems_list_events(int out_fd) {
  char OP_CODE = EMS_LIST_CODE;
  size_t num_events;
  unsigned int* ids;

  if (write(fd_req, &OP_CODE, sizeof(char)) == -1) {
    fprintf(stderr, "Failed to write the OP_CODE on the request pipe.\n");
    return 1;
  }
  
  int return_value;
  if (read(fd_resp, &return_value, sizeof(int)) == -1) {
    fprintf(stderr, "Failed to read response sent by server.\n");
    return 1;
  }

  if (return_value == SUCCESS_MSG) {
    if (read(fd_resp, &num_events, sizeof(size_t)) == -1) {
      fprintf(stderr, "Failed to read the number of events from the response pipe.\n");
      return 1;
    }
    ids = (unsigned int*) malloc(sizeof(unsigned int) * num_events);
    if (read(fd_resp, ids, sizeof(unsigned int) * (num_events)) == -1) {
      fprintf(stderr, "Failed to read the ids of the events from the response pipe.\n");
      return 1;
    }
    fprintf(stderr, "fd -> %d.\n", out_fd);
  }
  else {
    fprintf(stderr, "Failed to show an event on client %d.\n", session_id);
    return 1;
  }

  for (int i = 0; i < (int) num_events; i++) {
    char event_string[] = "Event: ";
    char id = (char)(ids[i] + '0');
    char end_line = '\n';

    if(write(out_fd, &event_string, sizeof(event_string) - 1) == -1) {
      fprintf(stderr, "Failed to write event in .out file.\n");
    }
    if(write(out_fd, &id, sizeof(id)) == -1) {
      fprintf(stderr, "Failed to write event in .out file.\n");
    }
    if(write(out_fd, &end_line, sizeof(end_line)) == -1) {
      fprintf(stderr, "Failed to write event in .out file.\n");
    }
  }
  return 0;
}
