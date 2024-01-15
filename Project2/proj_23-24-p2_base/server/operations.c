#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "common/io.h"
#include "eventlist.h"

static struct EventList* event_list = NULL;
static unsigned int state_access_delay_us = 0;

/// Gets the event with the given ID from the state.
/// @note Will wait to simulate a real system accessing a costly memory resource.
/// @param event_id The ID of the event to get.
/// @param from First node to be searched.
/// @param to Last node to be searched.
/// @return Pointer to the event if found, NULL otherwise.
static struct Event* get_event_with_delay(unsigned int event_id, struct ListNode* from, struct ListNode* to) {
  struct timespec delay = {0, state_access_delay_us * 1000};
  nanosleep(&delay, NULL);  // Should not be removed

  return get_event(event_list, event_id, from, to);
}

/// Gets the index of a seat.
/// @note This function assumes that the seat exists.
/// @param event Event to get the seat index from.
/// @param row Row of the seat.
/// @param col Column of the seat.
/// @return Index of the seat.
static size_t seat_index(struct Event* event, size_t row, size_t col) { return (row - 1) * event->cols + col - 1; }

int ems_init(unsigned int delay_us) {
  if (event_list != NULL) {
    fprintf(stderr, "EMS state has already been initialized\n");
    return 1;
  }

  event_list = create_list();
  state_access_delay_us = delay_us;

  return event_list == NULL;
}

int ems_terminate() {
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  if (pthread_rwlock_wrlock(&event_list->rwl) != 0) {
    fprintf(stderr, "Error locking list rwl\n");
    return 1;
  }

  free_list(event_list);
  pthread_rwlock_unlock(&event_list->rwl);
  return 0;
}

int ems_create(unsigned int event_id, size_t num_rows, size_t num_cols) {
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  if (pthread_rwlock_wrlock(&event_list->rwl) != 0) {
    fprintf(stderr, "Error locking list rwl\n");
    return 1;
  }

  if (get_event_with_delay(event_id, event_list->head, event_list->tail) != NULL) {
    fprintf(stderr, "Event already exists\n");
    pthread_rwlock_unlock(&event_list->rwl);
    return 1;
  }

  struct Event* event = malloc(sizeof(struct Event));

  if (event == NULL) {
    fprintf(stderr, "Error allocating memory for event\n");
    pthread_rwlock_unlock(&event_list->rwl);
    return 1;
  }

  event->id = event_id;
  event->rows = num_rows;
  event->cols = num_cols;
  event->reservations = 0;
  if (pthread_mutex_init(&event->mutex, NULL) != 0) {
    pthread_rwlock_unlock(&event_list->rwl);
    free(event);
    return 1;
  }
  event->data = calloc(num_rows * num_cols, sizeof(unsigned int));

  if (event->data == NULL) {
    fprintf(stderr, "Error allocating memory for event data\n");
    pthread_rwlock_unlock(&event_list->rwl);
    free(event);
    return 1;
  }

  if (append_to_list(event_list, event) != 0) {
    fprintf(stderr, "Error appending event to list\n");
    pthread_rwlock_unlock(&event_list->rwl);
    free(event->data);
    free(event);
    return 1;
  }

  pthread_rwlock_unlock(&event_list->rwl);
  return 0;
}

int ems_reserve(unsigned int event_id, size_t num_seats, size_t* xs, size_t* ys) {
  
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  if (pthread_rwlock_rdlock(&event_list->rwl) != 0) {
    fprintf(stderr, "Error locking list rwl\n");
    return 1;
  }

  struct Event* event = get_event_with_delay(event_id, event_list->head, event_list->tail);

  pthread_rwlock_unlock(&event_list->rwl);

  if (event == NULL) {
    fprintf(stderr, "Event not found\n");
    return 1;
  }

  if (pthread_mutex_lock(&event->mutex) != 0) {
    fprintf(stderr, "Error locking mutex\n");
    return 1;
  }
  printf("num seats -> %ld", num_seats);
  for (size_t i = 0; i < num_seats; i++) {
    printf("xs[i] -> %ld, ys[i] -> %ld\n", xs[i], ys[i]);
    if (xs[i] <= 0 || xs[i] > event->rows || ys[i] <= 0 || ys[i] > event->cols) {
      fprintf(stderr, "Seat out of bounds\n");
      pthread_mutex_unlock(&event->mutex);
      return 1;
    }
  }

  for (size_t i = 0; i < event->rows * event->cols; i++) {
    for (size_t j = 0; j < num_seats; j++) {
      if (seat_index(event, xs[j], ys[j]) != i) {
        continue;
      }

      if (event->data[i] != 0) {
        fprintf(stderr, "Seat already reserved\n");
        pthread_mutex_unlock(&event->mutex);
        return 1;
      }

      break;
    }
  }

  unsigned int reservation_id = ++event->reservations;

  for (size_t i = 0; i < num_seats; i++) {
    event->data[seat_index(event, xs[i], ys[i])] = reservation_id;
  }

  pthread_mutex_unlock(&event->mutex);
  return 0;
}

int ems_show(int resp_fd, unsigned int event_id) {
  int return_value = 0;

  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return_value = 1;
    
    if (write(resp_fd, &return_value, sizeof(int)) == -1) {
      perror("Failed to write the return value to the response pipe.\n");
      pthread_rwlock_unlock(&event_list->rwl);
      return 1;
    }
    return 1;
  }

  if (pthread_rwlock_rdlock(&event_list->rwl) != 0) {
    fprintf(stderr, "Error locking list rwl\n");
    return_value = 1;
    if (write(resp_fd, &return_value, sizeof(int)) == -1) {
      perror("Failed to write the return value to the response pipe.\n");
      pthread_rwlock_unlock(&event_list->rwl);
      return 1;
    }
    return 1;
  }
 
  struct Event* event = get_event_with_delay(event_id, event_list->head, event_list->tail);

  pthread_rwlock_unlock(&event_list->rwl);

  if (event == NULL) {
    fprintf(stderr, "Event not found\n");
    return_value = 1;
    if (write(resp_fd, &return_value, sizeof(int)) == -1) {
      perror("Failed to write the return value to the response pipe.\n");
      pthread_rwlock_unlock(&event_list->rwl);
      return 1;
    }
    return 1;
  }

  if (pthread_mutex_lock(&event->mutex) != 0) {
    fprintf(stderr, "Error locking mutex\n");
    return_value = 1;
    if (write(resp_fd, &return_value, sizeof(int)) == -1) {
      perror("Failed to write the return value to the response pipe.\n");
      pthread_rwlock_unlock(&event_list->rwl);
      return 1;
    }
    return 1;
  }

  size_t num_rows = event->rows;
  size_t num_cols = event->cols;
  unsigned int* seats = (unsigned int*) malloc(sizeof(unsigned int) * (num_rows * num_cols));

  // FIXME memcpy?
  for (size_t i = 1; i <= event->rows; i++) {
    for (size_t j = 1; j <= event->cols; j++) {
      seats[seat_index(event, i, j)] = event->data[seat_index(event, i, j)];
    }
  }
  // memcpy(seats, event->data, sizeof(unsigned int) * (num_rows * num_cols));

  if (write(resp_fd, &return_value, sizeof(int)) == -1) {
    perror("Failed to write the return value to the response pipe.\n");
    pthread_rwlock_unlock(&event_list->rwl);
    return 1;
  }
  if (write(resp_fd, &num_rows, sizeof(size_t)) == -1) {
    perror("Failed to write the number of rows on the response pipe.\n");
    pthread_rwlock_unlock(&event_list->rwl);
    return 1;
  }
  if (write(resp_fd, &num_cols, sizeof(size_t)) == -1) {
    perror("Failed to write the number of cols on the response pipe.\n");
    pthread_rwlock_unlock(&event_list->rwl);
    return 1;
  }

  if (write(resp_fd, seats, sizeof(unsigned int) * (num_rows * num_cols)) == -1) {
    perror("Failed to write the seats on the response pipe.\n");
    pthread_rwlock_unlock(&event_list->rwl);
    return 1;
  }

  pthread_mutex_unlock(&event->mutex);
  return 0;
}

int ems_list_events(int resp_fd) {
  int return_value = 0;

  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return_value = 1;
    if (write(resp_fd, &return_value, sizeof(int)) == -1) {
      perror("Failed to write the return value to the response pipe.\n");
      pthread_rwlock_unlock(&event_list->rwl);
      return 1;
    }
    return 1;
  }

  if (pthread_rwlock_rdlock(&event_list->rwl) != 0) {
    fprintf(stderr, "Error locking list rwl\n");
    return_value = 1;
    if (write(resp_fd, &return_value, sizeof(int)) == -1) {
      perror("Failed to write the return value to the response pipe.\n");
      pthread_rwlock_unlock(&event_list->rwl);
      return 1;
    }
    return 1;
  }

  struct ListNode* to = event_list->tail;
  struct ListNode* current = event_list->head;

  unsigned int* ids = (unsigned int*) malloc(sizeof(unsigned int));
  size_t num_events = 0;

  // NO EVENTS REGISTERED IN SERVER
  if (current == NULL) {
    if (write(resp_fd, &return_value, sizeof(int)) == -1) {
      perror("Failed to write the return value to the response pipe.\n");
      pthread_rwlock_unlock(&event_list->rwl);
      return 1;
    }
    if (write(resp_fd, &num_events, sizeof(char)) == -1) {
      perror("Failed to write the number of events on the response pipe.\n");
      pthread_rwlock_unlock(&event_list->rwl);
      return 1;
    }
    pthread_rwlock_unlock(&event_list->rwl);
    return 0;
  }
  //

  while (1) {
    ids = (unsigned int*) realloc(ids, sizeof(unsigned int) * (num_events+1));
    if (ids == NULL) {
      return_value = 1;
      if (write(resp_fd, &return_value, sizeof(int)) == -1) {
        perror("Failed to write the return value to the response pipe.\n");
        pthread_rwlock_unlock(&event_list->rwl);
        return 1;
      }
      perror("Failed to alloc memory for the ids array.\n");
      pthread_rwlock_unlock(&event_list->rwl);
      return 1;
    }
    ids[num_events] = (current->event)->id;
    num_events++;

    if (current == to) {
      break;
    }
    current = current->next;
  }

  // Success
  if (write(resp_fd, &return_value, sizeof(int)) == -1) {
    perror("Failed to write the return value to the response pipe.\n");
    pthread_rwlock_unlock(&event_list->rwl);
    return 1;
  }

  if (write(resp_fd, &num_events, sizeof(size_t)) == -1) {
    perror("Failed to write the number of events to the response pipe.\n");
    pthread_rwlock_unlock(&event_list->rwl);
    return 1;
  }

  if (write(resp_fd, ids, sizeof(unsigned int) * num_events) == -1) {
    perror("Failed to write the id list to the response pipe.\n");
    pthread_rwlock_unlock(&event_list->rwl);
    return 1;
  }

  pthread_rwlock_unlock(&event_list->rwl);
  return 0;
}