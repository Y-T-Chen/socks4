/**********************************************************************
 * DESCRIPTION:
 *   Serial Concurrent Wave Equation - C Version
 *   This program implements the concurrent wave equation
 *********************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#define MAXPOINTS 1000000
#define MAXSTEPS 1000000
#define MINPOINTS 20
#define PI 3.14159265

#define TILE_WIDTH 64

void check_param(void);
void printfinal (void);

int nsteps,                 	/* number of time steps */
    tpoints, 	     		/* total points along string */
    rcode;                  	/* generic return code */
float  values[MAXPOINTS+2], 	/* values at time t */
       oldval[MAXPOINTS+2], 	/* values at time (t-dt) */
       newval[MAXPOINTS+2]; 	/* values at time (t+dt) */


/*********************************************************************
 *	Checks input values from parameters
*********************************************************************/
void check_param(void)
{
    char tchar[20];

    /* check number of points, number of iterations */
    while ((tpoints < MINPOINTS) || (tpoints > MAXPOINTS))
    {
        printf("Enter number of points along vibrating string [%d-%d]: ", MINPOINTS, MAXPOINTS);
        scanf("%s", tchar);
        tpoints = atoi(tchar);
        if ((tpoints < MINPOINTS) || (tpoints > MAXPOINTS))
            printf("Invalid. Please enter value between %d and %d\n", MINPOINTS, MAXPOINTS);
    }
    while ((nsteps < 1) || (nsteps > MAXSTEPS))
    {
        printf("Enter number of time steps [1-%d]: ", MAXSTEPS);
        scanf("%s", tchar);
        nsteps = atoi(tchar);
        if ((nsteps < 1) || (nsteps > MAXSTEPS))
            printf("Invalid. Please enter value between 1 and %d\n", MAXSTEPS);
    }

    printf("Using points = %d, steps = %d\n", tpoints, nsteps);

}

/*********************************************************************
 *     Initialize points on line
 ********************************************************************/

/*********************************************************************
 *      Calculate new values using wave equation
 ********************************************************************/

/*********************************************************************
 *     Update all values along line a specified number of times
 ********************************************************************/
__global__ void update(float *d_values, const int d_tpoints, const int d_nsteps)
{
    int i = blockIdx.x*blockDim.x + threadIdx.x;
    int j;
    float d_values_reg, d_oldval_reg, d_newval_reg;

    /* Update points along line for this time step */
    if(i>0 && i<=d_tpoints)
    {
        /* Calculate initial values based on sine curve */
        d_values_reg = sin( (2.0*PI) * (float)(i-1)/(d_tpoints-1) );
        d_oldval_reg = d_values_reg;

        /* Update values for each time step */
        for (j = 1; j<= d_nsteps; j++)
        {
            /* global endpoints */
            if ((i == 1) || (i  == d_tpoints))
            {
                d_newval_reg = 0.0;
            }
            else
            {
                d_newval_reg = d_values_reg + d_values_reg - d_oldval_reg - 0.18*d_values_reg;
            }
            d_oldval_reg = d_values_reg;
            d_values_reg = d_newval_reg;
        }
        d_values[i] = d_values_reg;
    }

}

/*********************************************************************
 *     Print final results
 ********************************************************************/
void printfinal()
{
    int i;
    for (i = 1; i <= tpoints; i++)
    {
        printf("%6.4f ", values[i]);
        if (i%10 == 0)
            printf("\n");
    }
}

/*********************************************************************
 *	Main program
 ********************************************************************/
int main(int argc, char *argv[])
{
	sscanf(argv[1],"%d",&tpoints);
	sscanf(argv[2],"%d",&nsteps);

	check_param();

	dim3 threadPerBlock( TILE_WIDTH);
	dim3 numBlocks( (tpoints + (TILE_WIDTH-1) )/TILE_WIDTH);


    float *d_values;
    cudaMalloc( &d_values, tpoints*sizeof(float));


	update<<< numBlocks, threadPerBlock>>>(d_values, tpoints, nsteps);

    cudaMemcpy(values, d_values, tpoints*sizeof(float), cudaMemcpyDeviceToHost);

	printfinal();

	return 0;
}

