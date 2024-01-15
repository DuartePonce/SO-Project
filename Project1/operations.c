#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

#include <limits.h>

#include "eventlist.h"
#include "operations.h"

static struct EventList* event_list = NULL;
static unsigned int state_access_delay_ms = 0;

/// Calculates a timespec from a delay in milliseconds.
/// @param delay_ms Delay in milliseconds.
/// @return Timespec with the given delay.
static struct timespec delay_to_timespec(unsigned int delay_ms) {
	return (struct timespec){delay_ms / 1000, (delay_ms % 1000) * 1000000};
}

/// Gets the event with the given ID from the state.
/// @note Will wait to simulate a real system accessing a costly memory resource.
/// @param event_id The ID of the event to get.
/// @return Pointer to the event if found, NULL otherwise.
static struct Event* get_event_with_delay(unsigned int event_id) {
	struct timespec delay = delay_to_timespec(state_access_delay_ms);
	nanosleep(&delay, NULL);  // Should not be removed

	return get_event(event_list, event_id);
}

/// Gets the seat with the given index from the state.
/// @note Will wait to simulate a real system accessing a costly memory resource.
/// @param event Event to get the seat from.
/// @param index Index of the seat to get.
/// @return Pointer to the seat.
static unsigned int* get_seat_with_delay(struct Event* event, size_t index) {
	struct timespec delay = delay_to_timespec(state_access_delay_ms);
	nanosleep(&delay, NULL);  // Should not be removed

	return &event->data[index];
}

/// Gets the index of a seat.
/// @note This function assumes that the seat exists.
/// @param event Event to get the seat index from.
/// @param row Row of the seat.
/// @param col Column of the seat.
/// @return Index of the seat.
static size_t seat_index(struct Event* event, size_t row, size_t col) { return (row - 1) * event->cols + col - 1; }

int ems_init(unsigned int delay_ms) {
	if (event_list != NULL) {
		fprintf(stderr, "EMS state has already been initialized\n");
		return 1;
	}

	event_list = create_list();
	state_access_delay_ms = delay_ms;

	return event_list == NULL;
}

int ems_terminate() {
	if (event_list == NULL) {
		fprintf(stderr, "EMS state must be initialized\n");
		return 1;
	}

	free_list(event_list);
	return 0;
}

int ems_create(unsigned int event_id, size_t num_rows, size_t num_cols) {
	if (event_list == NULL) {
		fprintf(stderr, "EMS state must be initialized\n");
		return 1;
	}

	if (get_event_with_delay(event_id) != NULL) {
		fprintf(stderr, "Event already exists\n");
		return 1;
	}

	struct Event* event = malloc(sizeof(struct Event));

	if (event == NULL) {
		fprintf(stderr, "Error allocating memory for event\n");
		return 1;
	} 

	event->id = event_id;
	event->rows = num_rows;
	event->cols = num_cols;
	event->reservations = 0;
	event->data = malloc(num_rows * num_cols * sizeof(unsigned int));

	if (event->data == NULL) {
		fprintf(stderr, "Error allocating memory for event data\n");
		free(event);
		return 1;
	}

	for (size_t i = 0; i < num_rows * num_cols; i++) {
		event->data[i] = 0;
	}

	if (append_to_list(event_list, event) != 0) {
		fprintf(stderr, "Error appending event to list\n");
		free(event->data);
		free(event);
		return 1;
	}

	return 0;
}

int ems_reserve(unsigned int event_id, size_t num_seats, size_t* xs, size_t* ys) {
	if (event_list == NULL) {
		fprintf(stderr, "EMS state must be initialized\n");
		return 1;
	}

	struct Event* event = get_event_with_delay(event_id);

	if (event == NULL) {
		fprintf(stderr, "Event not found\n");
		return 1;
	}

	unsigned int reservation_id = ++event->reservations;

	size_t i = 0;
	for (; i < num_seats; i++) {
		size_t row = xs[i];
		size_t col = ys[i];

		if (row <= 0 || row > event->rows || col <= 0 || col > event->cols) {
			fprintf(stderr, "Invalid seat\n");
			break;
		}

		if (*get_seat_with_delay(event, seat_index(event, row, col)) != 0) {
			fprintf(stderr, "Seat already reserved\n");
			break;
		}

		*get_seat_with_delay(event, seat_index(event, row, col)) = reservation_id;
	}

	// If the reservation was not successful, free the seats that were reserved.
	if (i < num_seats) {
		event->reservations--;
		for (size_t j = 0; j < i; j++) {
			*get_seat_with_delay(event, seat_index(event, xs[j], ys[j])) = 0;
		}
		return 1;
	}

	return 0;
}


int ems_show(unsigned int event_id, char* in_file) {

	if (event_list == NULL) {
		fprintf(stderr, "EMS state must be initialized\n");
		return 1;
	}

	struct Event* event = get_event_with_delay(event_id);

	if (event == NULL) {
		fprintf(stderr, "Event not found\n");
		return 1;
	}
	
	// Abre o ficheiro .result respetivo ao ficheiro que usou este comando
	char* file_path = get_out_file(in_file);
	// printf("-----%s----\n", file_path);

	int fd = open(file_path, O_CREAT | O_WRONLY | O_APPEND, S_IRUSR | S_IWUSR);
	free(file_path);

	// Escreve no ficheiro a matriz que representa a ocupacao do evento
	for (size_t i = 1; i <= event->rows; i++) {
		for (size_t j = 1; j <= event->cols; j++) {
			unsigned int* seat = get_seat_with_delay(event, seat_index(event, i, j));

				int num_digits = snprintf(NULL, 0, "%u", *seat);

				char* buffer = malloc((size_t) (num_digits + 1)); 

				if (buffer != NULL) {
					int written = sprintf(buffer, "%u", *seat);

					if (written > 0) {
							write(fd, buffer, (size_t)written);

							if (j < event->cols) {
									write(fd, " ", 1);
							} else {
									write(fd, "\n", 1);
							}
					}
					
					free(buffer);
				}
		}
	}
	
	close(fd);
	return 0;
}

int ems_list_events(char* in_file) {

	if (event_list == NULL) {
		fprintf(stderr, "EMS state must be initialized\n");
		return 1;
	}

	if (event_list->head == NULL) {
		printf("No events\n");
		return 0;
	}

	struct ListNode* current = event_list->head;
	char* event_str = "Event: ";

	char* file_path = get_out_file(in_file);
	int fd = open(file_path, O_CREAT | O_WRONLY | O_APPEND, S_IRUSR | S_IWUSR);
	free(file_path);

	while (current != NULL) {
		char* buffer = malloc((size_t) (sizeof(event_str) + (current->event)->id) + 2);
		int written = sprintf(buffer, "%s %u\n", event_str, (current->event)->id);
		
		write(fd, buffer, (size_t)written);
		current = current->next;
		free(buffer);
	}
	close(fd);
	return 0;
} 

void ems_wait(unsigned int delay_ms) {
	struct timespec delay = delay_to_timespec(delay_ms);
	nanosleep(&delay, NULL);
}

// ** //

char* get_out_file(char *in_file) {

    char* name = NULL; 
    char* position = strstr(in_file, ".");
    
    size_t length = (size_t) (position - in_file);
    name = (char*) malloc((length + 5) * sizeof(char));

    strncpy(name, in_file, length);
    name[length] = '\0';

    snprintf(name + length, 5, ".out");

    return name;
}

void clear_previous_state(char *in_file) {

	char* file_path = get_out_file(in_file);
	// If file exists and has issues truncating, STDERR
	if (access(file_path, F_OK) != -1 && truncate(file_path, 0) == -1){
		fprintf(stderr, "Cannot truncate\n");
	}
	
	free(file_path);
}