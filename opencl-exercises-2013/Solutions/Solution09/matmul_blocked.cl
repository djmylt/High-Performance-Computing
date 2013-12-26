//-------------------------------------------------------------
//
//  PROGRAM: Blocked Matrix Multipliplication kernel
//
//  PURPOSE: Computes an element of the proudct matrix
//
//              C = A * B
//
//           Using the well known blocked algorithm.  
//
//           To derive this algorithm, start with the naive
//           triply nested loop algorithm with a dot product 
//           for each element of C.  Decompose each loop 
//           into blocks of size blcksz.  This gives you 6
//           nested loops with three loops over blocks
//           and three loops over indices inside the blocks.
// 
//           Rearrange the loops to put the 3 loops over blocks 
//           at the outermost loops of the loop nest.  You'll
//           see that the three "inner" loops are just the 
//           regular matrix product between blocks.
//
//           The algorithms is simple.  Keeping all the indices
//           straight is not.  We will use the following 
//           conventions:
//
//             i,j,k            ... indices of full, global matrices 
//             Iblk, Jblk, Kblk ... indices of matrix blocks
//             iloc, jloc, kloc ... indices inside blocks
//                 
//  HISTORY: Written by Tim Mattson, November 2013 
//-------------------------------------------------------------

// It turns out that the compiler generates much better code if
// we "hardwire" this block size.  16 works well for an NVIDIA 
// GPU, 32 works well for a CPU
#define blksz 16

__kernel void mmul(
                __global const float* A,
                __global const float* B,
                __global float* C,
                __local  float* Awrk,
                __local  float* Bwrk)
{
    int kloc, Kblk;
    float Ctmp=0.0f;

    //  This work-item will compute element C(i,j)
    const int i = get_global_id(0);
    const int j = get_global_id(1);
    const int N = get_global_size(0);  // assumes square matrices!

    // Element C(i,j) is in block C(Iblk,Jblk)
    const int Iblk = get_group_id(0);
    const int Jblk = get_group_id(1);

    // C(i,j) is element C(iloc, jloc) of block C(Iblk, Jblk)
    const int iloc = get_local_id(0);
    const int jloc = get_local_id(1);

    // the number of blocks are the same in each dimension
    const int Num_BLK = N/blksz;

    // Setup the upper-left-corner (base address) for the A and
    // B blocks plus the increments to advance base addresses as
    // we loop over blocks
    int Abase = Iblk*N*blksz;    
    const int Ainc  = blksz;

    int Bbase = Jblk*blksz;
    const int Binc  = blksz*N;


    // C(Iblk,Jblk) = (sum over KBLK) A(Iblk,Kblk)*B(Kblk,Jblk)
    for (Kblk = 0;  Kblk<Num_BLK;  Kblk++)
    {
       //Load A(Iblk,Kblk) and B(Kblk,Jblk).
       //Each work-item loads a single element of the two blocks
       //which are shared with the entire work-group

       Awrk[jloc*blksz+iloc] = A[Abase+jloc*N+iloc];
       Bwrk[jloc*blksz+iloc] = B[Bbase+jloc*N+iloc];

       // make sure all writes to local memory complete before we proceed
       barrier(CLK_LOCAL_MEM_FENCE);

       // Compute dot products over local blocks to find
       // the contribution to C(i,j) from this block
       #pragma unroll
       for(kloc=0; kloc<blksz; kloc++)
          Ctmp += Awrk[jloc*blksz+kloc] * Bwrk[kloc*blksz+iloc];

       // make sure all reads from local memory complete before we overwrite Awrk and Bwrk
       barrier(CLK_LOCAL_MEM_FENCE);

       Abase += Ainc;
       Bbase += Binc;
    }
 
    // update global C matrix 
    C[j*N+i] = Ctmp;

}
