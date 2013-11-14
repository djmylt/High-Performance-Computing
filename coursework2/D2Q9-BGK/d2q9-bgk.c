/*
** Code to implement a d2q9-bgk lattice boltzmann scheme.
** 'd2' inidates a 2-dimensional grid, and
** 'q9' indicates 9 velocities per grid cell.
** 'bgk' refers to the Bhatnagar-Gross-Krook collision step.
**
** The 'speeds' in each cell are numbered as follows:
**
** 6 2 5
**  \|/
** 3-0-1
**  /|\
** 7 4 8
**
** A 2D grid:
**
**           cols
**       --- --- ---
**      | D | E | F |
** rows  --- --- ---
**      | A | B | C |
**       --- --- ---
**
** 'unwrapped' in row major order to give a 1D array:
**
**  --- --- --- --- --- ---
** | A | B | C | D | E | F |
**  --- --- --- --- --- ---
**
** Grid indicies are:
**
**          ny
**          ^       cols(jj)
**          |  ----- ----- -----
**          | | ... | ... | etc |
**          |  ----- ----- -----
** rows(ii) | | 1,0 | 1,1 | 1,2 |
**          |  ----- ----- -----
**          | | 0,0 | 0,1 | 0,2 |
**          |  ----- ----- -----
**          ----------------------> nx
**
** Note the names of the input parameter and obstacle files
** are passed on the command line, e.g.:
**
**   d2q9-bgk.exe input.params obstacles.dat
**
** Be sure to adjust the grid dimensions in the parameter file
** if you choose a different obstacle file.
*/

#include<stdio.h>
#include<stdlib.h>
#include<time.h>
#include<sys/time.h>
#include<sys/resource.h>
#include "mpi.h"

#define MASTER 0
#define NUMPARAMS 7
#define NSPEEDS         9
#define FINALSTATEFILE  "final_state.dat"
#define AVVELSFILE      "av_vels.dat"

/* struct to hold the parameter values */
typedef struct {
  int    nx;            /* no. of cells in x-direction */
  int    ny;            /* no. of cells in y-direction */
  int    maxIters;      /* no. of iterations */
  int    reynolds_dim;  /* dimension for Reynolds number */
  float density;       /* density per link */
  float accel;         /* density redistribution */
  float omega;         /* relaxation parameter */
} t_param;

/* struct to hold the 'speed' values */
typedef struct {
  float speeds[NSPEEDS];
} t_speed;

enum boolean { FALSE, TRUE };

/*
** function prototypes
*/

/* load params, allocate memory, load obstacles & initialise fluid particle densities */
int initialise(const char* paramfile, const char* obstaclefile,
               t_param* params, t_speed** cells_ptr, t_speed** tmp_cells_ptr,
               int** obstacles_ptr, float** av_vels_ptr, int size, int rank);

/* 
** The main calculation methods.
** timestep calls, in order, the functions:
** accelerate_flow(), propagate(), rebound() & collision()
*/
int timestep(const t_param params, t_speed* cells, t_speed* tmp_cells, int* obstacles, int size, int rank);
int accelerate_flow(const t_param params, t_speed* cells, int* obstacles);
int propagate(const t_param params, t_speed* cells, t_speed* tmp_cells, int size, int rank);
int rebound_or_collision(const t_param params, t_speed* cells, t_speed* tmp_cells, int* obstacles);
int write_values(const t_param params, t_speed* cells, int* obstacles, float* av_vels);

/* finalise, including freeing up allocated memory */
int finalise(const t_param* params, t_speed** cells_ptr, t_speed** tmp_cells_ptr,
             int** obstacles_ptr, float** av_vels_ptr);

/* Sum all the densities in the grid.
** The total should remain constant from one timestep to the next. */
float total_density(const t_param params, t_speed* cells, int size, int rank);

/* compute average velocity */
float av_velocity(const t_param params, t_speed* cells, int* obstacles, int size, int rank);

/* calculate Reynolds number */
float calc_reynolds(const t_param params, t_speed* cells, int* obstacles, int size, int rank);

/* utility functions */
void die(const char* message, const int line, const char *file);
void usage(const char* exe);

/*
** main program:
** initialise, timestep loop, finalise
*/
int main(int argc, char* argv[])
{
  char*    paramfile;         /* name of the input parameter file */
  char*    obstaclefile;      /* name of a the input obstacle file */
  t_param  params;            /* struct to hold parameter values */
  t_speed* cells     = NULL;  /* grid containing fluid densities */
  t_speed* tmp_cells = NULL;  /* scratch space */
  int*     obstacles = NULL;  /* grid indicating which cells are blocked */
  float*  av_vels   = NULL;  /* a record of the av. velocity computed for each timestep */
  int      ii;                /* generic counter */
  struct timeval timstr;      /* structure to hold elapsed time */
  struct rusage ru;           /* structure to hold CPU time--system and user */
  double tic = 0,toc;             /* floating point numbers to calculate elapsed wallclock time */
  double usrtim;              /* floating point number to record elapsed user CPU time */
  double systim;              /* floating point number to record elapsed system CPU time */
  int size, rank;

  /* parse the command line */
  if(argc != 3) {
    usage(argv[0]);
  }
  else{
    paramfile = argv[1];
    obstaclefile = argv[2];
  }
  MPI_Init(&argc, &argv);
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  /* initialise our data structures and load values from file */
  initialise(paramfile, obstaclefile, &params, &cells, &tmp_cells, &obstacles, &av_vels, size, rank);
  
  MPI_Finalize();
  
  return EXIT_SUCCESS;

  if (rank == MASTER) {
      /* iterate for maxIters timesteps */
      gettimeofday(&timstr,NULL);
      tic=timstr.tv_sec+(timstr.tv_usec/1000000.0);
  }

  for (ii=0;ii<params.maxIters;ii++) {
    timestep(params,cells,tmp_cells,obstacles, size, rank);
    av_vels[ii] = av_velocity(params,cells,obstacles, size, rank);
#ifdef DEBUG
    printf("==timestep: %d==\n",ii);
    printf("av velocity: %.12E\n", av_vels[ii]);
    printf("tot density: %.12E\n",total_density(params,cells, size, rank));
#endif
  }
  if (rank == MASTER) {
      gettimeofday(&timstr,NULL);
      toc=timstr.tv_sec+(timstr.tv_usec/1000000.0);
      getrusage(RUSAGE_SELF, &ru);
      timstr=ru.ru_utime;        
      usrtim=timstr.tv_sec+(timstr.tv_usec/1000000.0);
      timstr=ru.ru_stime;        
      systim=timstr.tv_sec+(timstr.tv_usec/1000000.0);
    
      /* write final values and free memory */
      printf("==done==\n");
      printf("Reynolds number:\t\t%.12E\n",calc_reynolds(params,cells,obstacles, size, rank));
      printf("Elapsed time:\t\t\t%.6lf (s)\n", toc-tic);
      printf("Elapsed user CPU time:\t\t%.6lf (s)\n", usrtim);
      printf("Elapsed system CPU time:\t%.6lf (s)\n", systim);
      write_values(params,cells,obstacles,av_vels);
  }
  finalise(&params, &cells, &tmp_cells, &obstacles, &av_vels);
  
  MPI_Finalize();
  
  return EXIT_SUCCESS;
}

int timestep(const t_param params, t_speed* cells, t_speed* tmp_cells, int* obstacles, int size, int rank)
{
  accelerate_flow(params,cells,obstacles);
  propagate(params,cells,tmp_cells, size, rank);
  rebound_or_collision(params,cells,tmp_cells,obstacles);
  return EXIT_SUCCESS; 
}

int accelerate_flow(const t_param params, t_speed* cells, int* obstacles)
{
  int ii,jj;     /* generic counters */
  float w1,w2;  /* weighting factors */
  
  /* compute weighting factors */
  w1 = params.density * params.accel / 9.0;
  w2 = params.density * params.accel / 36.0;

  /* modify the first column of the grid */
  jj=0;
  for(ii=0;ii<params.ny;ii++) {
    /* if the cell is not occupied and
    ** we don't send a density negative */
    if( !obstacles[ii*params.nx + jj] && 
        (cells[ii*params.nx + jj].speeds[3] - w1) > 0.0 &&
        (cells[ii*params.nx + jj].speeds[6] - w2) > 0.0 &&
        (cells[ii*params.nx + jj].speeds[7] - w2) > 0.0 ) {
      /* increase 'east-side' densities */
      cells[ii*params.nx + jj].speeds[1] += w1;
      cells[ii*params.nx + jj].speeds[5] += w2;
      cells[ii*params.nx + jj].speeds[8] += w2;
      /* decrease 'west-side' densities */
      cells[ii*params.nx + jj].speeds[3] -= w1;
      cells[ii*params.nx + jj].speeds[6] -= w2;
      cells[ii*params.nx + jj].speeds[7] -= w2;
    }
  }

  return EXIT_SUCCESS;
}

int propagate(const t_param params, t_speed* cells, t_speed* tmp_cells, int size, int rank)
{
  int ii,jj;            /* generic counters */
  int x_e,x_w,y_n,y_s;  /* indices of neighbouring cells */

  /* loop over _all_ cells */
  for(ii=0;ii<params.ny;ii++) {
    for(jj=0;jj<params.nx;jj++) {
      /* determine indices of axis-direction neighbours
      ** respecting periodic boundary conditions (wrap around) */
      y_n = (ii + 1) % params.ny;
      x_e = (jj + 1) % params.nx;
      y_s = (ii == 0) ? (ii + params.ny - 1) : (ii - 1);
      x_w = (jj == 0) ? (jj + params.nx - 1) : (jj - 1);
      /* propagate densities to neighbouring cells, following
      ** appropriate directions of travel and writing into
      ** scratch space grid */
      tmp_cells[ii *params.nx + jj].speeds[0]  = cells[ii*params.nx + jj].speeds[0]; /* central cell, */
                                                                                     /* no movement   */
      tmp_cells[ii *params.nx + jj].speeds[1] = cells[ii*params.nx + x_w].speeds[1]; /* east */
      tmp_cells[ii*params.nx + jj].speeds[2]  = cells[y_s*params.nx + jj].speeds[2]; /* north */
      tmp_cells[ii *params.nx + jj].speeds[3] = cells[ii*params.nx + x_e].speeds[3]; /* west */
      tmp_cells[ii*params.nx + jj].speeds[4]  = cells[y_n*params.nx + jj].speeds[4]; /* south */
      tmp_cells[ii*params.nx + jj].speeds[5] = cells[y_s*params.nx + x_w].speeds[5]; /* north-east */
      tmp_cells[ii*params.nx + jj].speeds[6] = cells[y_s*params.nx + x_e].speeds[6]; /* north-west */
      tmp_cells[ii*params.nx + jj].speeds[7] = cells[y_n*params.nx + x_e].speeds[7]; /* south-west */      
      tmp_cells[ii*params.nx + jj].speeds[8] = cells[y_n*params.nx + x_w].speeds[8]; /* south-east */      
    }
  }

  return EXIT_SUCCESS;
}

int rebound_or_collision(const t_param params, t_speed* cells, t_speed* tmp_cells, int* obstacles)
{
  int ii,jj,kk;                 /* generic counters */
  const float c_sq = 1.0/3.0;  /* square of speed of sound */
  const float w0 = 4.0/9.0;    /* weighting factor */
  const float w1 = 1.0/9.0;    /* weighting factor */
  const float w2 = 1.0/36.0;   /* weighting factor */
  float u_x,u_y;               /* av. velocities in x and y directions */
  float u[NSPEEDS];            /* directional velocities */
  float d_equ[NSPEEDS];        /* equilibrium densities */
  float u_sq;                  /* squared velocity */
  float local_density;         /* sum of densities in a particular cell */

  /* loop over the cells in the grid
  ** NB the collision step is called after
  ** the propagate step and so values of interest
  ** are in the scratch-space grid */
  for(ii=0;ii<params.ny;ii++) {
    for(jj=0;jj<params.nx;jj++) {
      /* if the cell contains an obstacle */
      if(obstacles[ii*params.nx + jj]) {
          /* called after propagate, so taking values from scratch space
          ** mirroring, and writing into main grid */
          cells[ii*params.nx + jj].speeds[1] = tmp_cells[ii*params.nx + jj].speeds[3];
          cells[ii*params.nx + jj].speeds[2] = tmp_cells[ii*params.nx + jj].speeds[4];
          cells[ii*params.nx + jj].speeds[3] = tmp_cells[ii*params.nx + jj].speeds[1];
          cells[ii*params.nx + jj].speeds[4] = tmp_cells[ii*params.nx + jj].speeds[2];
          cells[ii*params.nx + jj].speeds[5] = tmp_cells[ii*params.nx + jj].speeds[7];
          cells[ii*params.nx + jj].speeds[6] = tmp_cells[ii*params.nx + jj].speeds[8];
          cells[ii*params.nx + jj].speeds[7] = tmp_cells[ii*params.nx + jj].speeds[5];
          cells[ii*params.nx + jj].speeds[8] = tmp_cells[ii*params.nx + jj].speeds[6];
      } else {
          /* compute local density total */
          local_density = 0.0;
          for(kk=0;kk<NSPEEDS;kk++) {
            local_density += tmp_cells[ii*params.nx + jj].speeds[kk];
          }
          /* compute x velocity component */
          u_x = (tmp_cells[ii*params.nx + jj].speeds[1] +
                 tmp_cells[ii*params.nx + jj].speeds[5] +
                 tmp_cells[ii*params.nx + jj].speeds[8]
                 - (tmp_cells[ii*params.nx + jj].speeds[3] +
                    tmp_cells[ii*params.nx + jj].speeds[6] +
                    tmp_cells[ii*params.nx + jj].speeds[7]))
            / local_density;
          /* compute y velocity component */
          u_y = (tmp_cells[ii*params.nx + jj].speeds[2] +
                 tmp_cells[ii*params.nx + jj].speeds[5] +
                 tmp_cells[ii*params.nx + jj].speeds[6]
                 - (tmp_cells[ii*params.nx + jj].speeds[4] +
                    tmp_cells[ii*params.nx + jj].speeds[7] +
                    tmp_cells[ii*params.nx + jj].speeds[8]))
            / local_density;
          /* velocity squared */
          u_sq = u_x * u_x + u_y * u_y;
          /* directional velocity components */
          u[1] =   u_x;        /* east */
          u[2] =         u_y;  /* north */
          u[3] = - u_x;        /* west */
          u[4] =       - u_y;  /* south */
          u[5] =   u_x + u_y;  /* north-east */
          u[6] = - u_x + u_y;  /* north-west */
          u[7] = - u_x - u_y;  /* south-west */
          u[8] =   u_x - u_y;  /* south-east */
          /* equilibrium densities */
          /* zero velocity density: weight w0 */
          d_equ[0] = w0 * local_density * (1.0 - u_sq * (1.0 / (2.0 * c_sq)));
          /* axis speeds: weight w1 */
          d_equ[1] = w1 * local_density * (1.0 + u[1] * (1.0 / c_sq)
                           + (u[1] * u[1]) * (1.0 / (2.0 * c_sq * c_sq))
                           - u_sq * (1.0 / (2.0 * c_sq)));
          d_equ[2] = w1 * local_density * (1.0 + u[2] * (1.0 / c_sq)
                           + (u[2] * u[2]) * (1.0 / (2.0 * c_sq * c_sq))
                           - u_sq * (1.0 / (2.0 * c_sq)));
          d_equ[3] = w1 * local_density * (1.0 + u[3] * (1.0 / c_sq)
                           + (u[3] * u[3]) * (1.0 / (2.0 * c_sq * c_sq))
                           - u_sq * (1.0 / (2.0 * c_sq)));
          d_equ[4] = w1 * local_density * (1.0 + u[4] * (1.0 / c_sq)
                           + (u[4] * u[4]) * (1.0 / (2.0 * c_sq * c_sq))
                           - u_sq * (1.0 / (2.0 * c_sq)));
          /* diagonal speeds: weight w2 */
          d_equ[5] = w2 * local_density * (1.0 + u[5] * (1.0 / c_sq)
                           + (u[5] * u[5]) * (1.0 / (2.0 * c_sq * c_sq))
                           - u_sq * (1.0 / (2.0 * c_sq)));
          d_equ[6] = w2 * local_density * (1.0 + u[6] * (1.0 / c_sq)
                           + (u[6] * u[6]) * (1.0 / (2.0 * c_sq * c_sq))
                           - u_sq * (1.0 / (2.0 * c_sq)));
          d_equ[7] = w2 * local_density * (1.0 + u[7] * (1.0 / c_sq)
                           + (u[7] * u[7]) * (1.0 / (2.0 * c_sq * c_sq))
                           - u_sq * (1.0 / (2.0 * c_sq)));
          d_equ[8] = w2 * local_density * (1.0 + u[8] * (1.0 / c_sq)
                           + (u[8] * u[8]) * (1.0 / (2.0 * c_sq * c_sq))
                           - u_sq * (1.0 / (2.0 * c_sq)));
          /* relaxation step */
          for(kk=0;kk<NSPEEDS;kk++) {
            cells[ii*params.nx + jj].speeds[kk] = (tmp_cells[ii*params.nx + jj].speeds[kk]
                               + params.omega *
                               (d_equ[kk] - tmp_cells[ii*params.nx + jj].speeds[kk]));
          }
      }
    }
  }

  return EXIT_SUCCESS; 
}

int initialise(const char* paramfile, const char* obstaclefile,
               t_param* params, t_speed** cells_ptr, t_speed** tmp_cells_ptr,
               int** obstacles_ptr, float** av_vels_ptr, int size, int rank)
{
  char   message[1024];  /* message buffer */
  FILE   *fp;            /* file pointer */
  int    ii,jj;          /* generic counters */
  int    xx,yy;          /* generic array indices */
  int    blocked;        /* indicates whether a cell is blocked by an obstacle */ 
  int    retval;         /* to hold return value for checking */
  float w0,w1,w2;       /* weighting factors */

  if (rank == MASTER) {
      /* open the parameter file */
      fp = fopen(paramfile,"r");
      if (fp == NULL) {
        sprintf(message,"could not open input parameter file: %s", paramfile);
        die(message,__LINE__,__FILE__);
      }
    
      /* read in the parameter values */
      retval = fscanf(fp,"%d\n",&(params->nx));
      if(retval != 1) die ("could not read param file: nx",__LINE__,__FILE__);
      retval = fscanf(fp,"%d\n",&(params->ny));
      if(retval != 1) die ("could not read param file: ny",__LINE__,__FILE__);
      retval = fscanf(fp,"%d\n",&(params->maxIters));
      if(retval != 1) die ("could not read param file: maxIters",__LINE__,__FILE__);
      retval = fscanf(fp,"%d\n",&(params->reynolds_dim));
      if(retval != 1) die ("could not read param file: reynolds_dim",__LINE__,__FILE__);
      retval = fscanf(fp,"%f\n",&(params->density));
      if(retval != 1) die ("could not read param file: density",__LINE__,__FILE__);
      retval = fscanf(fp,"%f\n",&(params->accel));
      if(retval != 1) die ("could not read param file: accel",__LINE__,__FILE__);
      retval = fscanf(fp,"%f\n",&(params->omega));
      if(retval != 1) die ("could not read param file: omega",__LINE__,__FILE__);
    
      /* and close up the file */
      fclose(fp);
  }
  
  if (size > 1) {
      MPI_Aint base_addr, addr;
      MPI_Aint displacements[NUMPARAMS];
      MPI_Datatype types[NUMPARAMS];
      MPI_Datatype params_type;
      int block_lengths[NUMPARAMS];
      t_param send_params;
      if (rank == MASTER) {
          send_params.nx = params->nx;
          send_params.ny = params->ny / (size);
          send_params.maxIters = params->maxIters;
          send_params.reynolds_dim = params->reynolds_dim;
          send_params.density = params->density;
          send_params.accel = params->accel;
          send_params.omega = params->omega;
      }
      types[0] = MPI_INT;
      block_lengths[0] = 1;
      MPI_Address(&(send_params.nx), &base_addr);
      displacements[0] = 0;
      types[1] = MPI_INT;
      block_lengths[1] = 1;
      MPI_Address(&(send_params.ny), &addr);
      displacements[1] = addr - base_addr;
      types[2] = MPI_INT;
      block_lengths[2] = 1;
      MPI_Address(&(send_params.maxIters), &addr);
      displacements[2] = addr - base_addr;
      types[3] = MPI_FLOAT;
      block_lengths[3] = 1;
      MPI_Address(&(send_params.reynolds_dim), &addr);
      displacements[3] = addr - base_addr;
      types[4] = MPI_FLOAT;
      block_lengths[4] = 1;
      MPI_Address(&(send_params.density), &addr);
      displacements[4] = addr - base_addr;
      types[5] = MPI_FLOAT;
      block_lengths[5] = 1;
      MPI_Address(&(send_params.accel), &addr);
      displacements[5] = addr - base_addr;
      types[6] = MPI_FLOAT;
      block_lengths[6] = 1;
      MPI_Address(&(send_params.omega), &addr);
      displacements[6] = addr - base_addr;
      
      MPI_Type_create_struct(NUMPARAMS, block_lengths, displacements, types, &params_type);
      MPI_Type_commit(&params_type);
      MPI_Bcast(&send_params, 1, params_type, MASTER, MPI_COMM_WORLD);
      
      if (rank == MASTER) {
          params->ny = send_params.ny + (params->ny % size);
      } else {
          params->nx = send_params.nx;
          params->ny = send_params.ny;
          params->maxIters = send_params.maxIters;
          params->reynolds_dim = send_params.reynolds_dim;
          params->density = send_params.density;
          params->accel = send_params.accel;
          params->omega = send_params.omega;
      }
  }
  
  printf("%d %d %d %d %d %f %f %f\n", rank, params->nx, params->ny, params->maxIters, params->reynolds_dim, params->density, params->accel, params->omega);

  return EXIT_SUCCESS;

  /* 
  ** Allocate memory.
  **
  ** Remember C is pass-by-value, so we need to
  ** pass pointers into the initialise function.
  **
  ** NB we are allocating a 1D array, so that the
  ** memory will be contiguous.  We still want to
  ** index this memory as if it were a (row major
  ** ordered) 2D array, however.  We will perform
  ** some arithmetic using the row and column
  ** coordinates, inside the square brackets, when
  ** we want to access elements of this array.
  **
  ** Note also that we are using a structure to
  ** hold an array of 'speeds'.  We will allocate
  ** a 1D array of these structs.
  */

  /* main grid */
  *cells_ptr = (t_speed*)malloc(sizeof(t_speed)*((params->ny + 2)*params->nx));
  if (*cells_ptr == NULL) 
    die("cannot allocate memory for cells",__LINE__,__FILE__);

  /* 'helper' grid, used as scratch space */
  *tmp_cells_ptr = (t_speed*)malloc(sizeof(t_speed)*((params->ny + 2)*params->nx));
  if (*tmp_cells_ptr == NULL) 
    die("cannot allocate memory for tmp_cells",__LINE__,__FILE__);
  
  /* the map of obstacles */
  *obstacles_ptr = malloc(sizeof(int*)*(params->ny*params->nx));
  if (*obstacles_ptr == NULL) 
    die("cannot allocate column memory for obstacles",__LINE__,__FILE__);

  /* initialise densities */
  w0 = params->density * 4.0/9.0;
  w1 = params->density      /9.0;
  w2 = params->density      /36.0;

  for(ii=0;ii<params->ny;ii++) {
    for(jj=0;jj<params->nx;jj++) {
      /* centre */
      (*cells_ptr)[(ii + 1)*params->nx + jj].speeds[0] = w0;
      /* axis directions */
      (*cells_ptr)[(ii + 1)*params->nx + jj].speeds[1] = w1;
      (*cells_ptr)[(ii + 1)*params->nx + jj].speeds[2] = w1;
      (*cells_ptr)[(ii + 1)*params->nx + jj].speeds[3] = w1;
      (*cells_ptr)[(ii + 1)*params->nx + jj].speeds[4] = w1;
      /* diagonals */
      (*cells_ptr)[(ii + 1)*params->nx + jj].speeds[5] = w2;
      (*cells_ptr)[(ii + 1)*params->nx + jj].speeds[6] = w2;
      (*cells_ptr)[(ii + 1)*params->nx + jj].speeds[7] = w2;
      (*cells_ptr)[(ii + 1)*params->nx + jj].speeds[8] = w2;
    }
  }

  /* first set all cells in obstacle array to zero */ 
  for(ii=0;ii<params->ny;ii++) {
    for(jj=0;jj<params->nx;jj++) {
      (*obstacles_ptr)[ii*params->nx + jj] = 0;
    }
  }

  /* open the obstacle data file */
  fp = fopen(obstaclefile,"r");
  if (fp == NULL) {
    sprintf(message,"could not open input obstacles file: %s", obstaclefile);
    die(message,__LINE__,__FILE__);
  }

  /* read-in the blocked cells list */
  while( (retval = fscanf(fp,"%d %d %d\n", &xx, &yy, &blocked)) != EOF) {
    /* some checks */
    if ( retval != 3)
      die("expected 3 values per line in obstacle file",__LINE__,__FILE__);
    if ( xx<0 || xx>params->nx-1 )
      die("obstacle x-coord out of range",__LINE__,__FILE__);
    if ( yy<0 || yy>params->ny-1 )
      die("obstacle y-coord out of range",__LINE__,__FILE__);
    if ( blocked != 1 ) 
      die("obstacle blocked value should be 1",__LINE__,__FILE__);
    /* assign to array */
    (*obstacles_ptr)[yy*params->nx + xx] = blocked;
  }
  
  /* and close the file */
  fclose(fp);

  /* 
  ** allocate space to hold a record of the avarage velocities computed 
  ** at each timestep
  */
  *av_vels_ptr = (float*)malloc(sizeof(float)*params->maxIters);

  return EXIT_SUCCESS;
}

int finalise(const t_param* params, t_speed** cells_ptr, t_speed** tmp_cells_ptr,
             int** obstacles_ptr, float** av_vels_ptr)
{
  /* 
  ** free up allocated memory
  */
  free(*cells_ptr);
  *cells_ptr = NULL;

  free(*tmp_cells_ptr);
  *tmp_cells_ptr = NULL;

  free(*obstacles_ptr);
  *obstacles_ptr = NULL;

  free(*av_vels_ptr);
  *av_vels_ptr = NULL;

  return EXIT_SUCCESS;
}

float av_velocity(const t_param params, t_speed* cells, int* obstacles, int size, int rank)
{
  int    ii,jj,kk;       /* generic counters */
  int    tot_cells = 0;  /* no. of cells used in calculation */
  float local_density;  /* total density in cell */
  float tot_u_x;        /* accumulated x-components of velocity */

  /* initialise */
  tot_u_x = 0.0;

  /* loop over all non-blocked cells */
  for(ii=0;ii<params.ny;ii++) {
    for(jj=0;jj<params.nx;jj++) {
      /* ignore occupied cells */
      if(!obstacles[ii*params.nx + jj]) {
        /* local density total */
        local_density = 0.0;
        for(kk=0;kk<NSPEEDS;kk++) {
          local_density += cells[ii*params.nx + jj].speeds[kk];
        }
        /* x-component of velocity */
        tot_u_x += (cells[ii*params.nx + jj].speeds[1] +
                    cells[ii*params.nx + jj].speeds[5] +
                    cells[ii*params.nx + jj].speeds[8]
                    - (cells[ii*params.nx + jj].speeds[3] +
                       cells[ii*params.nx + jj].speeds[6] +
                       cells[ii*params.nx + jj].speeds[7])) /
          local_density;
        /* increase counter of inspected cells */
        ++tot_cells;
      }
    }
  }

  return tot_u_x / (float)tot_cells;
}

float calc_reynolds(const t_param params, t_speed* cells, int* obstacles, int size, int rank)
{
  const float viscosity = 1.0 / 6.0 * (2.0 / params.omega - 1.0);
  
  return av_velocity(params,cells,obstacles, size, rank) * params.reynolds_dim / viscosity;
}

float total_density(const t_param params, t_speed* cells, int size, int rank)
{
  int ii,jj,kk;        /* generic counters */
  float total = 0.0;  /* accumulator */

  for(ii=0;ii<params.ny;ii++) {
    for(jj=0;jj<params.nx;jj++) {
      for(kk=0;kk<NSPEEDS;kk++) {
        total += cells[ii*params.nx + jj].speeds[kk];
      }
    }
  }
  
  return total;
}

int write_values(const t_param params, t_speed* cells, int* obstacles, float* av_vels)
{
  FILE* fp;                     /* file pointer */
  int ii,jj,kk;                 /* generic counters */
  const float c_sq = 1.0/3.0;  /* sq. of speed of sound */
  float local_density;         /* per grid cell sum of densities */
  float pressure;              /* fluid pressure in grid cell */
  float u_x;                   /* x-component of velocity in grid cell */
  float u_y;                   /* y-component of velocity in grid cell */

  fp = fopen(FINALSTATEFILE,"w");
  if (fp == NULL) {
    die("could not open file output file",__LINE__,__FILE__);
  }

  for(ii=0;ii<params.ny;ii++) {
    for(jj=0;jj<params.nx;jj++) {
      /* an occupied cell */
      if(obstacles[ii*params.nx + jj]) {
        u_x = u_y = 0.0;
        pressure = params.density * c_sq;
      }
      /* no obstacle */
      else {
        local_density = 0.0;
        for(kk=0;kk<NSPEEDS;kk++) {
          local_density += cells[ii*params.nx + jj].speeds[kk];
        }
        /* compute x velocity component */
        u_x = (cells[ii*params.nx + jj].speeds[1] +
               cells[ii*params.nx + jj].speeds[5] +
               cells[ii*params.nx + jj].speeds[8]
               - (cells[ii*params.nx + jj].speeds[3] +
                  cells[ii*params.nx + jj].speeds[6] +
                  cells[ii*params.nx + jj].speeds[7]))
          / local_density;
        /* compute y velocity component */
        u_y = (cells[ii*params.nx + jj].speeds[2] +
               cells[ii*params.nx + jj].speeds[5] +
               cells[ii*params.nx + jj].speeds[6]
               - (cells[ii*params.nx + jj].speeds[4] +
                  cells[ii*params.nx + jj].speeds[7] +
                  cells[ii*params.nx + jj].speeds[8]))
          / local_density;
        /* compute pressure */
        pressure = local_density * c_sq;
      }
      /* write to file */
      fprintf(fp,"%d %d %.12E %.12E %.12E %d\n",ii,jj,u_x,u_y,pressure,obstacles[ii*params.nx + jj]);
    }
  }

  fclose(fp);

  fp = fopen(AVVELSFILE,"w");
  if (fp == NULL) {
    die("could not open file output file",__LINE__,__FILE__);
  }
  for (ii=0;ii<params.maxIters;ii++) {
    fprintf(fp,"%d:\t%.12E\n", ii, av_vels[ii]);
  }

  fclose(fp);

  return EXIT_SUCCESS;
}

void die(const char* message, const int line, const char *file)
{
  fprintf(stderr, "Error at line %d of file %s:\n", line, file);
  fprintf(stderr, "%s\n",message);
  fflush(stderr);
  exit(EXIT_FAILURE);
}

void usage(const char* exe)
{
  fprintf(stderr, "Usage: %s <paramfile> <obstaclefile>\n", exe);
  exit(EXIT_FAILURE);
}
