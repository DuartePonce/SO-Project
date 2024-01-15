#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <semaphore.h>
#include <string.h>
#include <sys/wait.h>
#include <pthread.h>

#include "constants.h"
#include "operations.h"
#include "parser.h"
#include "main.h"

int END = 1;

sem_t threads_sem; 
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

int main(int argc, char *argv[]) {
	unsigned int state_access_delay_ms = STATE_ACCESS_DELAY_MS;

	if (argc <= 3) {
		fprintf(stderr, "Insuficient arguments given\n");
		return 1;
	}

	char *jobs_path = argv[1];
	DIR *jobs_dir = opendir(jobs_path);

	if (jobs_dir == NULL) {
		fprintf(stderr, "Error opening the specified directory\n");
		return 1;
	}

	char *endptr;
	unsigned int MAX_PROC = (unsigned int) strtoul(argv[2], &endptr, 10);

	if (*endptr != '\0' || MAX_PROC < 1) {
		fprintf(stderr, "Invalid MAX_PROC value\n");
		return 1;
	}

	unsigned int MAX_THREADS = (unsigned int) strtoul(argv[3], &endptr, 10);

	if (*endptr != '\0' || MAX_THREADS < 1) {
		fprintf(stderr, "Invalid MAX_THREADS value\n");
		return 1;
	}
	
	if (argc == 5) {
		unsigned long int _delay = strtoul(argv[4], &endptr, 10);

		if (*endptr != '\0' || _delay > UINT_MAX) {
			fprintf(stderr, "Invalid delay value or value too large\n");
			return 1;
		}

		state_access_delay_ms = (unsigned int) _delay;
	}

	if (ems_init(state_access_delay_ms)) {
		fprintf(stderr, "Failed to initialize EMS\n");
		return 1;
	}

	struct dirent* dir_pointer;

	unsigned int process_counter = 0;
	while ((dir_pointer = readdir(jobs_dir)) != NULL) {
		if (strstr(dir_pointer->d_name, ".jobs")) {
			process_counter++;
			pid_t pid = fork();
			if (pid == -1) {
				perror("Error creating child process");
				exit(EXIT_FAILURE);
			}
			else if (pid == 0) {
				// Child process

				// Gathers the file_path
				// FIXME
				char file_path[PATH_MAX];
				snprintf(file_path, PATH_MAX, "%s/%s", jobs_path, dir_pointer->d_name);
				// printf("--%s\n--", file_path);

				clear_previous_state(file_path);
				ems_file_reader(file_path, MAX_THREADS);
				exit(EXIT_SUCCESS);

				free(file_path);
			}
			else {
				// Parent process
				if (process_counter % MAX_PROC == 0) {
					wait(NULL);
				}
			}
		}
	}
	closedir(jobs_dir);
	ems_terminate();
}

void ems_file_reader(char* file_path, unsigned int MAX_THREADS) {

	file_arg* file = (file_arg*) malloc(sizeof(file_arg));
	file->file_path = (char*) malloc(strlen(file_path) + 1);
	strcpy(file->file_path, file_path);
	file->line_to_read = 0;

	pthread_t *threads = NULL;

	pthread_mutex_init(&mutex, NULL);
	sem_init(&threads_sem, 0, MAX_THREADS);

	int tc = 0;
	END = 1;
	while (END) {
		if (END == 0) {
			break;
		}
		sem_wait(&threads_sem);
		threads = (pthread_t*) realloc(threads, (long unsigned) (tc+1) * sizeof(pthread_t));
		file_arg* thread_file = (file_arg*) malloc(sizeof(file_arg));
		memcpy(thread_file, file, sizeof(file_arg));

	

		thread_file->line_to_read = tc + 1;
		if (pthread_create(&threads[tc++], NULL, ems_command_processing, thread_file) != 0) {
			perror("Error creating thread");
			exit(EXIT_FAILURE);
		}
		sem_post(&threads_sem);
	}

	for (int i = 0; i < tc; i++) {
		if (pthread_join(threads[i], NULL) != 0) {
			perror("Error joining thread");
			exit(EXIT_FAILURE);
		}
	}

	pthread_mutex_destroy(&mutex);
	free(file->file_path);
	free(threads);
	free(file);
}

void* ems_command_processing(void* file_struct) {

	unsigned int event_id, delay;
	size_t num_rows, num_columns, num_coords;
	size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];

	file_arg* file = (file_arg*) file_struct;

	int line_to_read = file->line_to_read;
	int fd = open(file->file_path, O_RDONLY);
	int lines_read = 1;
	while (1) {
		enum Command user_input = get_next(fd);
		if (lines_read == line_to_read) {
			printf("> ");
			fflush(stdout);
			switch (user_input) {
				case CMD_CREATE:
					if (parse_create(fd, &event_id, &num_rows, &num_columns) != 0) {
						fprintf(stderr, "Invalid command. See HELP for usage\n");
					}
					if (ems_create(event_id, num_rows, num_columns)) {
						fprintf(stderr, "Failed to create event\n");
					}	
					break;

				case CMD_RESERVE:
					num_coords = parse_reserve(fd, MAX_RESERVATION_SIZE, &event_id, xs, ys);
					if (num_coords == 0) {
						fprintf(stderr, "Invalid command. See HELP for usage\n");	
					}

					if (ems_reserve(event_id, num_coords, xs, ys)) {
						fprintf(stderr, "Failed to reserve seats\n");
					}
					break;

				case CMD_SHOW:
					pthread_mutex_lock(&mutex);

					if (parse_show(fd, &event_id) != 0) {
						fprintf(stderr, "Invalid command. See HELP for usage\n");
					}

					if (ems_show(event_id, file->file_path)) {
						fprintf(stderr, "Failed to show event\n");
					}

					pthread_mutex_unlock(&mutex);
					break;

				case CMD_LIST_EVENTS:

					pthread_mutex_lock(&mutex);

					if (ems_list_events(file->file_path)) {
						fprintf(stderr, "Failed to list events\n");
					}

					pthread_mutex_unlock(&mutex);
					break;

				case CMD_WAIT:

					if (parse_wait(fd, &delay, NULL) == -1) {  // thread_id is not implemented
						fprintf(stderr, "Invalid command. See HELP for usage\n");
					
					}

					if (delay > 0) {
						printf("Waiting...\n");
						fflush(stdout);
						ems_wait(delay);
					}

					break;

				case CMD_INVALID:
					fprintf(stderr, "Invalid command. See HELP for usage\n");
					break;

				case CMD_HELP:
					printf(
					"Available commands:\n"
					"  CREATE <event_id> <num_rows> <num_columns>\n"
					"  RESERVE <event_id> [(<x1>,<y1>) (<x2>,<y2>) ...]\n"
					"  SHOW <event_id>\n"
					"  LIST\n"
					"  WAIT <delay_ms> [thread_id]\n"  // thread_id is not implemented
					"  BARRIER\n"                      // Not implemented
					"  HELP\n");

					break;

				case CMD_BARRIER:
					pthread_mutex_lock(&mutex);
					printf("Barrier doing stuff ....");

					pthread_mutex_unlock(&mutex);
					break; 

				case CMD_EMPTY:
					break;

				case EOC:
					printf("ola\n");
					END = 0;
					close(fd);
					free(file); // FIXME
					pthread_exit(NULL);
					break;

			}
		}
		lines_read++;
	}
	printf("-%d-\n", lines_read);
	close(fd);
	free(file); // FIXME
	pthread_exit(NULL);
}
