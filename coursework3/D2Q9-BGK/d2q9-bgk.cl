#define NSPEEDS         9

/* struct to hold the 'speed' values */
typedef struct {
  float speeds[NSPEEDS];
} t_speed;

__kernel void accelerate_flow(const int nx, const float density, const float accel, __global t_speed *cells, __global int *obstacles)
{
  int ii,jj;     /* generic counters */
  float w1,w2;  /* weighting factors */
  
  /* compute weighting factors */
  w1 = density * accel / 9.0;
  w2 = density * accel / 36.0;

  /* modify the first column of the grid */
  jj=0;
  ii = get_global_id(0);
  /* if the cell is not occupied and
  ** we don't send a density negative */
  if( !obstacles[ii*nx + jj] && 
      (cells[ii*nx + jj].speeds[3] - w1) > 0.0 &&
      (cells[ii*nx + jj].speeds[6] - w2) > 0.0 &&
      (cells[ii*nx + jj].speeds[7] - w2) > 0.0 ) {
    /* increase 'east-side' densities */
    cells[ii*nx + jj].speeds[1] += w1;
    cells[ii*nx + jj].speeds[5] += w2;
    cells[ii*nx + jj].speeds[8] += w2;
    /* decrease 'west-side' densities */
    cells[ii*nx + jj].speeds[3] -= w1;
    cells[ii*nx + jj].speeds[6] -= w2;
    cells[ii*nx + jj].speeds[7] -= w2;
  }
}

__kernel void propagate(__global t_speed *cells, __global t_speed *tmp_cells)
{
  int ii,jj,nx,ny;            /* generic counters */
  int x_e,x_w,y_n,y_s;  /* indices of neighbouring cells */

  /* determine indices of axis-direction neighbours
  ** respecting periodic boundary conditions (wrap around) */
  ii = get_global_id(0);
  jj = get_global_id(1);
  ny = get_global_size(0);
  nx = get_global_size(1);
  y_n = (ii + 1) % ny;
  x_e = (jj + 1) % nx;
  y_s = (ii == 0) ? (ii + ny - 1) : (ii - 1);
  x_w = (jj == 0) ? (jj + nx - 1) : (jj - 1);
  /* propagate densities to neighbouring cells, following
  ** appropriate directions of travel and writing into
  ** scratch space grid */
  tmp_cells[ii *nx + jj].speeds[0]  = cells[ii*nx + jj].speeds[0]; /* central cell, */
                                                                                 /* no movement   */
  tmp_cells[ii *nx + jj].speeds[1] = cells[ii*nx + x_w].speeds[1]; /* east */
  tmp_cells[ii*nx + jj].speeds[2]  = cells[y_s*nx + jj].speeds[2]; /* north */
  tmp_cells[ii *nx + jj].speeds[3] = cells[ii*nx + x_e].speeds[3]; /* west */
  tmp_cells[ii*nx + jj].speeds[4]  = cells[y_n*nx + jj].speeds[4]; /* south */
  tmp_cells[ii*nx + jj].speeds[5] = cells[y_s*nx + x_w].speeds[5]; /* north-east */
  tmp_cells[ii*nx + jj].speeds[6] = cells[y_s*nx + x_e].speeds[6]; /* north-west */
  tmp_cells[ii*nx + jj].speeds[7] = cells[y_n*nx + x_e].speeds[7]; /* south-west */      
  tmp_cells[ii*nx + jj].speeds[8] = cells[y_n*nx + x_w].speeds[8]; /* south-east */   
}

__kernel void rebound_or_collision(const float omega, __global t_speed *cells, __global t_speed *tmp_cells, __global int *obstacles)
{
  int ii,jj,kk,nx,ny;                 /* generic counters */
  const float c_sq = 1.0/3.0;  /* square of speed of sound */
  const float w0 = 4.0/9.0;    /* weighting factor */
  const float w1 = 1.0/9.0;    /* weighting factor */
  const float w2 = 1.0/36.0;   /* weighting factor */
  float u_x,u_y;               /* av. velocities in x and y directions */
  float u[NSPEEDS];            /* directional velocities */
  float d_equ[NSPEEDS];        /* equilibrium densities */
  float u_sq;                  /* squared velocity */
  float local_density;         /* sum of densities in a particular cell */

  ii = get_global_id(0);
  jj = get_global_id(1);
  ny = get_global_size(0);
  nx = get_global_size(1);

  /* loop over the cells in the grid
  ** NB the collision step is called after
  ** the propagate step and so values of interest
  ** are in the scratch-space grid */
  /* if the cell contains an obstacle */
  if(obstacles[ii*nx + jj]) {
      /* called after propagate, so taking values from scratch space
      ** mirroring, and writing into main grid */
      cells[ii*nx + jj].speeds[1] = tmp_cells[ii*nx + jj].speeds[3];
      cells[ii*nx + jj].speeds[2] = tmp_cells[ii*nx + jj].speeds[4];
      cells[ii*nx + jj].speeds[3] = tmp_cells[ii*nx + jj].speeds[1];
      cells[ii*nx + jj].speeds[4] = tmp_cells[ii*nx + jj].speeds[2];
      cells[ii*nx + jj].speeds[5] = tmp_cells[ii*nx + jj].speeds[7];
      cells[ii*nx + jj].speeds[6] = tmp_cells[ii*nx + jj].speeds[8];
      cells[ii*nx + jj].speeds[7] = tmp_cells[ii*nx + jj].speeds[5];
      cells[ii*nx + jj].speeds[8] = tmp_cells[ii*nx + jj].speeds[6];
  } else {
      /* compute local density total */
      local_density = 0.0;
      for(kk=0;kk<NSPEEDS;kk++) {
        local_density += tmp_cells[ii*nx + jj].speeds[kk];
      }
      /* compute x velocity component */
      u_x = (tmp_cells[ii*nx + jj].speeds[1] +
             tmp_cells[ii*nx + jj].speeds[5] +
             tmp_cells[ii*nx + jj].speeds[8]
             - (tmp_cells[ii*nx + jj].speeds[3] +
                tmp_cells[ii*nx + jj].speeds[6] +
                tmp_cells[ii*nx + jj].speeds[7]))
        / local_density;
      /* compute y velocity component */
      u_y = (tmp_cells[ii*nx + jj].speeds[2] +
             tmp_cells[ii*nx + jj].speeds[5] +
             tmp_cells[ii*nx + jj].speeds[6]
             - (tmp_cells[ii*nx + jj].speeds[4] +
                tmp_cells[ii*nx + jj].speeds[7] +
                tmp_cells[ii*nx + jj].speeds[8]))
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
        cells[ii*nx + jj].speeds[kk] = (tmp_cells[ii*nx + jj].speeds[kk]
                           + omega *
                           (d_equ[kk] - tmp_cells[ii*nx + jj].speeds[kk]));
      }
   }
}

__kernel void sum_velocity(__global t_speed *cells, global int *obstacles, __local float* scratch, __const int length, __global float* result) {
  int global_index = get_global_id(0);
  int kk;
  float local_density;
  float accumulator = 0;
  // Loop sequentially over chunks of input vector
  while (global_index < length) {
    if (!obstacles[global_index]) {
      /* local density total */
     local_density = 0.0;
     for(kk=0;kk<NSPEEDS;kk++) {
        local_density += cells[global_index].speeds[kk];
      }
      /* x-component of velocity */
      accumulator += (cells[global_index].speeds[1] +
                  cells[global_index].speeds[5] +
                  cells[global_index].speeds[8]
                  - (cells[global_index].speeds[3] +
                     cells[global_index].speeds[6] +
                     cells[global_index].speeds[7])) /
        local_density;
    }
    global_index += get_global_size(0);
  }

  // Perform parallel reduction
  int local_index = get_local_id(0);
  scratch[local_index] = accumulator;
  barrier(CLK_LOCAL_MEM_FENCE);
  for(int offset = get_local_size(0) / 2; offset > 0; offset = offset / 2) {
    if (local_index < offset) {
      scratch[local_index] += scratch[local_index + offset];
    }
    barrier(CLK_LOCAL_MEM_FENCE);
  }
  if (local_index == 0) {
    result[get_group_id(0)] = scratch[0];
  }
}

__kernel void second_sum(__local float* scratch, __const int length, __global float* result) {
  int global_index = get_global_id(0);
  int kk;
  float local_density;
  float accumulator = 0;
  // Loop sequentially over chunks of input vector
  while (global_index < length) {
    accumulator += result[global_index];
    global_index += get_global_size(0);
  }

  // Perform parallel reduction
  int local_index = get_local_id(0);
  scratch[local_index] = accumulator;
  barrier(CLK_LOCAL_MEM_FENCE);
  for(int offset = get_local_size(0) / 2; offset > 0; offset = offset / 2) {
    if (local_index < offset) {
      scratch[local_index] += scratch[local_index + offset];
    }
    barrier(CLK_LOCAL_MEM_FENCE);
  }
  if (local_index == 0) {
    result[get_group_id(0)] = scratch[0];
  }
}