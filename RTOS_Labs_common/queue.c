#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "queue.h"


void queue_create(queue_t* queue)
{
  // queue_t queue = malloc(sizeof(struct queue));
  // if(queue == NULL) return NULL;

  // queue->arr_to_hold_queue = malloc(sizeof(void*) * QUEUE_STARTING_SIZE);
  // if(queue->arr_to_hold_queue == NULL) {
  //   free(queue); //need to free the struct if the array alloc failed.
  //   return NULL;
  // }

  queue->max_size = QUEUE_STARTING_SIZE;
  queue->curr_size = 0;
  queue->front = queue->arr_to_hold_queue;
  queue->back = queue->arr_to_hold_queue;
  // return queue;
}

// int queue_destroy(queue_t queue)
// {
//   if(queue == NULL || queue->curr_size != 0) return -1;
//   free(queue->arr_to_hold_queue);
//   free(queue);
//   return 0;
// }

int queue_enqueue(queue_t* queue, void *data)
{
  if(queue == NULL || data == NULL) return -1;

  // no malloc so expansion disabled
  if(queue->curr_size == queue->max_size) //queue is full and needs to be expanded
  {
    // void **new_array = malloc(sizeof(void*) * queue->max_size *2);
    // if(new_array == NULL) return -1;

    // int i = 0;
    // void **queue_array_end = queue->arr_to_hold_queue + queue->max_size;
    // do //copy all data into new array
    // {
    //   new_array[i++] = *(queue->front++);
    //   // check if pointer needs to wrap around the array
    //   if(queue->front == queue_array_end)
    //     queue->front = queue->arr_to_hold_queue;
    // } while (queue->front != queue->back); //repeat until the front pointer loops around to back

    // // update struct variables
    // free(queue->arr_to_hold_queue);
    // queue->front = new_array;
    // queue->back = new_array + i;
    // queue->arr_to_hold_queue = new_array;
    // queue->max_size *= 2;
    return -1;
  }
  //enqueue data
  *(queue->back++) = data;
  if(queue->back - queue->arr_to_hold_queue >= queue->max_size)
    queue->back = queue->arr_to_hold_queue;
  queue->curr_size++;
  return 0;
}

int queue_dequeue(queue_t* queue, void **data)
{
  if(queue == NULL || data == NULL || queue->curr_size == 0) return -1;
  *data = *(queue->front++);
  if(queue->front == queue->arr_to_hold_queue + queue->max_size)
    queue->front = queue->arr_to_hold_queue;
  queue->curr_size--;
  return 0;
}

int queue_delete(queue_t* queue, void *data)
{
  if(queue == NULL || data == NULL || queue->curr_size == 0) return -1;
  void **iter = queue->front;
  int found = 0;
  void **queue_array_end = queue->arr_to_hold_queue + queue->max_size - 1; //pointer to last spot in the array
  do //find the data
  {
    if(*iter == data)
      found = 1;
    if(iter == queue_array_end)
      iter = queue->arr_to_hold_queue;
    else iter++;
  } while(iter != queue->back && !found);
  
  if(!found) return -1;
  // TODO: does something need to be done to the deleted data?

  // shift all the remaing 
  while(iter != queue->back)
  {
    if(iter == queue->arr_to_hold_queue)
      *queue_array_end = *iter;
    else *(iter-1) = *iter;

    if(iter == queue_array_end)
      iter = queue->arr_to_hold_queue;
    else iter++;
  }

  if(queue->back == queue->arr_to_hold_queue)
    queue->back = queue_array_end;
  else queue->back--;
  queue->curr_size--;
  return 0;
}

int simple_queue_iterate(queue_t* queue, queue_func_t func)
{
  int i = 0;
  int old_size = queue->curr_size;
  while(i < old_size)
  {
    func(queue, queue->front[i]);
    if(old_size != queue->curr_size)
      old_size = queue->curr_size;
    else i++;
  }
  return 0;
}

int queue_iterate(queue_t* queue, queue_func_t func)
{
  if(queue == NULL || func == NULL || queue->curr_size == 0) return -1;
  if(queue->back - queue->front > 0)
    return simple_queue_iterate(queue, func);
  void **iter = queue->front;
  void **queue_array_end = queue->arr_to_hold_queue + queue->max_size - 1; //pointer to last spot in the array
  int old_size = queue->curr_size;
  do //find the data
  {
    func(queue, *iter);
    if(old_size != queue->curr_size)
      old_size = queue->curr_size;
    else if(iter == queue_array_end)
      iter = queue->arr_to_hold_queue;
    else iter++;
  } while(iter != queue->back);

  return 0;
}

int queue_length(queue_t* queue)
{
  if(queue == NULL) return -1;
  return queue->curr_size;
}
