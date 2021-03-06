/*
** Sending simple, point-to-point messages.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "mpi.h" 

int main(int argc, char* argv[])
{
  int myrank;
  int size;
  int left;              /* destination rank for message */
  int right;            /* source rank of a message */
  int sendtag = 0;           /* scope for adding extra information to a message */
  int recvtag = 0;
  MPI_Status status;     /* struct used by MPI_Recv */
  char message_sent[BUFSIZ], message_received[BUFSIZ];

  /* MPI_Init returns once it has started up processes */
  MPI_Init( &argc, &argv );

  /* size and rank will become ubiquitous */ 
  MPI_Comm_size( MPI_COMM_WORLD, &size );
  MPI_Comm_rank( MPI_COMM_WORLD, &myrank );

  sprintf(message_sent, "Come-in Danny-Boy, this is process %d!", myrank);
  int half = size / 2;

  if (myrank < half) {
    if (myrank % half == 0) {
      left = myrank + half;
      right = myrank + 1;
    } else {
      left = myrank - 1;
      right = (myrank == half - 1) ? myrank + half : myrank + 1;
    }
  } else {
    if (myrank % half == 0) {
      left = myrank + 1;
      right = myrank - half;
    } else {
      left = (myrank == size - 1) ? myrank - half : myrank + 1;
      right = myrank - 1;
    }
  }

  /* 
  ** SPMD - conditionals based upon rank
  ** will also become ubiquitous
  */
  MPI_Sendrecv(message_sent,strlen(message_sent)+1, MPI_CHAR, right, sendtag, message_received, BUFSIZ, MPI_CHAR, left, recvtag, MPI_COMM_WORLD, &status);
  printf("process %d recieved message: %s\n", myrank, message_received);
  MPI_Sendrecv(message_sent,strlen(message_sent)+1, MPI_CHAR, left, sendtag, message_received, BUFSIZ, MPI_CHAR, right, recvtag, MPI_COMM_WORLD, &status);
  printf("process %d recieved message: %s\n", myrank, message_received);

  /* don't forget to tidy up when we're done */
  MPI_Finalize();

  /* and exit the program */
  return EXIT_SUCCESS;
}
