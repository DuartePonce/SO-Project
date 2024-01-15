#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <semaphore.h>

#include "common/constants.h"
#include "common/io.h"
#include "operations.h"
#include "main.h"

struct client_info {
	int sv_fd;
	int session_id;
};

// Mutexes and semaphores
pthread_mutex_t mutex_session;
sem_t sem_empty;
sem_t sem_full;

int sessions = 0;
struct client_info client_buffer[MAX_SESSION_COUNT];
int buffer_count = 0;

char* server_pipe_path;

int main(int argc, char* argv[]) {

	if (argc < 2 || argc > 3) {
		fprintf(stderr, "Usage: %s\n <pipe_path> [delay]\n", argv[0]);
		return 1;
	}

	server_pipe_path = argv[1];

	char* endptr;
	unsigned int state_access_delay_us = STATE_ACCESS_DELAY_US;
	if (argc == 3) {
		unsigned long int delay = strtoul(argv[2], &endptr, 10);

		if (*endptr != '\0' || delay > UINT_MAX) {
			fprintf(stderr, "Invalid delay value or value too large\n");
			return 1;
		}

		state_access_delay_us = (unsigned int)delay;
	}

	if (ems_init(state_access_delay_us)) {
		fprintf(stderr, "Failed to initialize EMS\n");
		return 1;
	}

	unlink(server_pipe_path);

	if (mkfifo(server_pipe_path, S_IRUSR | S_IWUSR | S_IRGRP) != 0) {
		fprintf(stderr, "Creation of server pipe failed in directory %s.\n", server_pipe_path);
		return 1;
	}

	///

	pthread_t threads[MAX_SESSION_COUNT];
	pthread_mutex_init(&mutex_session, NULL);
    sem_init(&sem_empty, 0, MAX_SESSION_COUNT);
    sem_init(&sem_full, 0, 0);

	for (int thread_id = 0; thread_id < MAX_SESSION_COUNT; thread_id++) {
		if (thread_id > 0) {
			if (pthread_create(&threads[thread_id], NULL, &client_reader, NULL) != 0) {
				fprintf(stderr, "Failed to create reader thread in the %d index of the threads array.", thread_id);
				return 1;
			}
		}
		else {
			if (pthread_create(&threads[thread_id], NULL, &client_listener, NULL) != 0) {
				fprintf(stderr, "Failed to create listener thread in the %d index of the threads array.", thread_id);
				return 1;
			}
		}
  	}
	for (int i = 0; i < MAX_SESSION_COUNT; i++) {
        if (pthread_join(threads[i], NULL) != 0) {
            perror("Failed to join thread");
        }
    }

	sem_destroy(&sem_empty);
	sem_destroy(&sem_full);
	pthread_mutex_destroy(&mutex_session);
	ems_terminate();

	return 0;
}

void* client_listener() {
	while (1) {
		// Wait for new clients
        int sv_fd = open(server_pipe_path, O_RDONLY);
		printf("opened, again!\n");
		sleep(1);
        
		sem_wait(&sem_empty);
        pthread_mutex_lock(&mutex_session);
        client_buffer[buffer_count].sv_fd = sv_fd;
		client_buffer[buffer_count].session_id = sessions;
        buffer_count++;
		sessions++;
        pthread_mutex_unlock(&mutex_session);
        sem_post(&sem_full);

		close(sv_fd);
	}
}

void* client_reader() {
	while (1) {
		int sv_fd, session_id;
		// Remove from the buffer
        sem_wait(&sem_full);
        pthread_mutex_lock(&mutex_session);
        sv_fd = client_buffer[buffer_count - 1].sv_fd;
		session_id = client_buffer[buffer_count - 1].session_id;
        buffer_count--;
		fprintf(stderr, "Passed! %d\n", buffer_count);

		pthread_mutex_unlock(&mutex_session);
        sem_post(&sem_empty);

		char OP_CODE;
		if (read(sv_fd, &OP_CODE, sizeof(char)) == -1) {
			fprintf(stderr, "Failed to read OP_CODE sent by server. %d\n", session_id);
		}
		if (OP_CODE != EMS_SETUP_CODE) {
			fprintf(stderr, "Failed to set up the client: code received (%d) wasn't meant for setup.\n", OP_CODE);
		}

		char* req_pipe_path = (char*) malloc(sizeof(char) * MAX_PIPENAME_SIZE);
		if (read(sv_fd, req_pipe_path, sizeof(char) * MAX_PIPENAME_SIZE) == -1) {
			fprintf(stderr, "Failed reading the request pipe path from the server pipe. Expected valid path, found %s.\n", req_pipe_path);
		}
		char* resp_pipe_path = (char*) malloc(sizeof(char) * MAX_PIPENAME_SIZE);
		if (read(sv_fd, resp_pipe_path, sizeof(char) * MAX_PIPENAME_SIZE) == -1) {
			fprintf(stderr, "Failed reading the response pipe path from the server pipe. Expected valid path, found %s.\n", resp_pipe_path);
		}

		// Required work with sv_fd done
		close(sv_fd);

		int req_fd = open(req_pipe_path, O_RDONLY);
		if (req_fd == -1) {
			fprintf(stderr, "Failed to open the request pipe on path \"%s\".\n", req_pipe_path);
		}

		int resp_fd = open(resp_pipe_path, O_WRONLY);
		if (resp_fd == -1) {
			fprintf(stderr, "Failed to open the response pipe on path \"%s\".\n", resp_pipe_path);
		}

    	if (write(resp_fd, &session_id, sizeof(int)) == -1) {
      		fprintf(stderr, "Failed to write the response pipe path on the server pipe.\n");
    	}

		int failed_value = FAIL_MSG;
		int success_value = SUCCESS_MSG;

		unsigned int event_id;
		size_t num_seats;
		size_t num_rows;
		size_t num_cols;
		size_t* xs = NULL;
		size_t* ys = NULL;

		int comands = EOC;
		while (comands) {
			if (read(req_fd, &OP_CODE, sizeof(char)) == -1) {
				fprintf(stderr, "Failed reading the OP_CODE (%d).\n", OP_CODE);
				break;
			}
			fprintf(stderr, "Working with OP_CODE %d.\n", OP_CODE);
			switch (OP_CODE) {
			case EMS_CREATE_CODE:

				if (read(req_fd, &event_id, sizeof(unsigned int)) == -1) {
					write(resp_fd, &failed_value, sizeof(int));
					break;
				}
				if (read(req_fd, &num_rows, sizeof(size_t)) == -1) {
					write(resp_fd, &failed_value, sizeof(int));
					break;
				}
				if (read(req_fd, &num_cols, sizeof(size_t)) == -1) {
					write(resp_fd, &failed_value, sizeof(int));
					break;
				}
				
				if (ems_create(event_id, num_rows, num_cols) == -1) {
					write(resp_fd, &failed_value, sizeof(int));
					break;
				}  
				write(resp_fd, &success_value, sizeof(int));
				break;
			
			case EMS_QUIT_CODE:

				fprintf(stderr, "Ended reading file!\n");
				close(req_fd);
				close(resp_fd);
				comands = 0;
				break;

			case EMS_RESERVE_CODE:

				if (read(req_fd, &event_id, sizeof(unsigned int)) == -1) {
					write(resp_fd, &failed_value, sizeof(int));
					break;
				}
				if (read(req_fd, &num_seats, sizeof(size_t)) == -1) {
					write(resp_fd, &failed_value, sizeof(int));
					break;
				}

				xs = (size_t*) malloc(sizeof(size_t) * num_seats);
				if (read(req_fd, xs, sizeof(size_t) * num_seats) == -1) {
					write(resp_fd, &failed_value, sizeof(int));
					break;
				}

				ys = (size_t*) malloc(sizeof(size_t) * num_seats);
				if (read(req_fd, ys, sizeof(size_t) * num_seats) == -1) {
					write(resp_fd, &failed_value, sizeof(int));
					break;
				}

				if (ems_reserve(event_id, num_seats, xs, ys) == -1) {
					write(resp_fd, &failed_value, sizeof(int));
					break;
				}
				write(resp_fd, &success_value, sizeof(int));
				break;

			case EMS_SHOW_CODE:
				if (read(req_fd, &event_id, sizeof(unsigned int)) == -1) {
					write(resp_fd, &failed_value, sizeof(int));
					break;
				}
				ems_show(resp_fd, event_id); 
				break;

			case EMS_LIST_CODE:
				ems_list_events(resp_fd);
				break;

			default:
				break;
			}
		}
	}
}
