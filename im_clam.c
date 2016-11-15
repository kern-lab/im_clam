//im_clam.c --
// A. Kern 3/20/15
//
// Composite Likelihood estimation of canonical IM models via AFS

//going to use NLopt for optimization

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_blas.h>
#include <gsl/gsl_rng.h>
#include <gsl/gsl_randist.h>
#include <nlopt.h> 
#include "AFS.h"
#include "adkGSL.h"
#include "cs.h"
#include "time.h"
#include <unistd.h>
#include "adkCSparse.h"
#include "AFS_ctmc.h"
#include <slepcmfn.h>
#include "AFS_ctmc_petsc.h"
#include "im_clam.h"

void import2DSFSData(const char *fileName, gsl_matrix *obsData);

int n1, n2;

afsStateSpace *stateSpace, *reducedStateSpace;

clam_lik_params *currentParams, *nextParams;

gsl_matrix *transMat;
double *res;

double lowerBounds[5] = {0.01,0.01,0.0,0.0,0.001};
double upperBounds[5] = {10.0,10.0,20.0,20.0,10.0};
PetscBool vbse = PETSC_FALSE;

static char help[] = "im_clam\n\
	Example: mpiexec -n <np> ./im_clam -s <stateSpace file> -m <mats file> -d <data file> \n\n\toptions:\n\
	\t-exp expected value mode (requires -x flag too)\n\
	\t-mo multiple optimizations from different start points\n\
	\t-global multi-level optimization (MLSL algo.)\n\
	\t-x <theta_2, theta_A, mig12, mig21, t_div> parameter starting values\n\
	\t-obs (prints out observed AFS as well as that expected from MLE params)\n\
	\t-u mutation rate per base pair per generation (only used to unscale parameters; default 1e-8)\n\
	\t-g generation time (gens/year; default 20)\n\
	\t-r randomSeed\n\
	\t-v verbose output\n";
	


int main(int argc, char **argv){
	int i,j, N,runMode;
	clock_t time1, time2;
	int nnz;
	int seed;
	char  filename[PETSC_MAX_PATH_LEN], filename2[PETSC_MAX_PATH_LEN], filename3[PETSC_MAX_PATH_LEN] ;
	double lik = 0.0;
	double mle[5] = {0.1,2,1,1,5};
 	const gsl_rng_type * T;
  	gsl_rng * r;
	PetscErrorCode ierr;
	PetscMPIInt    rank,size;
	PetscBool      flg,obsFlag;
	int dim=5;
	double snpNumber, pi_est, p, N0;
	double u=1e-8;
	double genPerYear=20;
	gsl_matrix *fi;
	FILE *infile;
	
	///////////////////
	/////	PETSC / Slepc library version of this code
	SlepcInitialize(&argc,&argv,(char*)0,help);
	ierr = MPI_Comm_rank(PETSC_COMM_WORLD, &rank);CHKERRQ(ierr);
	ierr = MPI_Comm_size(PETSC_COMM_WORLD, &size);CHKERRQ(ierr);
	if(rank==0){

		printf("\n.___   _____             .__\n|   | /     \\       ____ |  | _____    _____\n|   |/  \\ /  \\    _/ ___\\|  | \\__  \\  /     \\\n|   /    Y    \\   \\  \\___|  |__/ __ \\|  Y Y  \\\n|___\\____|__  /____\\___  >____(____  /__|_|  /\n            \\/_____/   \\/          \\/      \\/\n\n\n");


		printf("im_clam -- Isolation with Migration Composite Likelihood Analysis using Markov chains\n");
		//printf("A.D. Kern 2015\n///////////////////\n");
		printf("\n\n");
	}
	if(argc<2){
		printf("%s",help);
		exit(666);
	}
	time1=clock();
	
	ierr = PetscOptionsGetString(NULL,"-s",filename,PETSC_MAX_PATH_LEN,&flg);CHKERRQ(ierr);
	ierr = PetscOptionsGetString(NULL,"-m",filename2,PETSC_MAX_PATH_LEN,&flg);CHKERRQ(ierr);
	ierr = PetscOptionsGetString(NULL,"-d",filename3,PETSC_MAX_PATH_LEN,&flg);CHKERRQ(ierr);
	ierr = PetscOptionsGetInt(NULL,"-r",&seed,&flg);CHKERRQ(ierr);
	ierr = PetscOptionsGetReal(NULL,"-u",&u,&flg);CHKERRQ(ierr);
	ierr = PetscOptionsGetReal(NULL,"-g",&genPerYear,&flg);CHKERRQ(ierr);
	if(!flg) seed=time(NULL);
	
	//printf("rank:%d %d\n",rank,seed);
	MPI_Bcast(&seed,1,MPI_INT,0,PETSC_COMM_WORLD);
	//printf("rank:%d %d\n",rank,seed);
	
	runMode = 1;
	obsFlag=PETSC_FALSE;
	
	//setup RNG for starting point for optimization
	gsl_rng_env_setup();
	T = gsl_rng_default;
	r = gsl_rng_alloc (T);
	gsl_rng_set(r,seed);
	
	for(i=0;i<dim;i++){
		mle[i] = gsl_ran_flat(r,lowerBounds[i], upperBounds[i]);
	}
	
	PetscOptionsHasName(NULL,"-v",&flg); if(flg) vbse=PETSC_TRUE;
	PetscOptionsHasName(NULL,"-obs",&flg); if(flg) obsFlag=PETSC_TRUE;
	
	PetscOptionsHasName(NULL,"-exp",&flg); if(flg) runMode=2;
	ierr = PetscOptionsGetRealArray(NULL,"-x",mle,&dim,&flg);CHKERRQ(ierr);

	//double check we are set for exp mode
	if(runMode == 2 && !flg){
		printf("for exp runmode need to set parameter values using -x flag\n");
		exit(1);
	}
	
	
	PetscOptionsHasName(NULL,"-mo",&flg); if(flg) runMode=3;
	PetscOptionsHasName(NULL,"-global",&flg); if(flg) runMode=4;
	
	
	//import state space
	stateSpace = afsStateSpaceImportFromFile(filename);
	N = stateSpace->nstates;


	//quick peak at the mats file to set nnz
	//open file
	infile = fopen(filename2, "r");
	if (infile == NULL){
		fprintf(stderr,"Error opening mats file! ARRRRR!!!!\n");
		exit(1);
	}
	fscanf(infile,"nnz: %d", &nnz);
	fclose(infile);
	//got nnz, time for allocs
	
	//setup currentParams struct; alloc all arrays
	currentParams = malloc(sizeof(clam_lik_params));
	currentParams->n1 = n1 = stateSpace->states[0]->popMats[0]->size1 - 1;
	currentParams->n2 = n2 = stateSpace->states[0]->popMats[1]->size2 - 1;
	currentParams->stateSpace = stateSpace;
	currentParams->rank = rank;
//	currentParams->snpNumber = snpNumber;
	currentParams->expAFS = gsl_matrix_alloc(n1+1,n2+1);
	currentParams->expAFS2 = gsl_matrix_alloc(n1+1,n2+1);
	currentParams->map = malloc(sizeof(int)*N);
	currentParams->reverseMap = malloc(sizeof(int)*N);
	currentParams->rates = gsl_vector_alloc(stateSpace->nstates);
	currentParams->stateVec = gsl_vector_alloc(stateSpace->nstates);
	currentParams->resVec = gsl_vector_alloc(stateSpace->nstates);
	gsl_vector_set_zero(currentParams->resVec);
//	currentParams->paramVector = gsl_vector_alloc(5);
//	currentParams->paramCIUpper = gsl_vector_alloc(5);
//	currentParams->paramCILower = gsl_vector_alloc(5);
//	currentParams->mlParams = gsl_vector_alloc(5);

	currentParams->top = malloc(sizeof(double) * nnz);
	currentParams->topA = malloc(sizeof(double) * nnz);

	currentParams->move = malloc(sizeof(int) * nnz);
	currentParams->moveA = malloc(sizeof(int) * nnz);
//	printf("allocated moveType...\n");
	for(i=0;i< (nnz);i++){
		currentParams->move[i]=0;
		currentParams->top[i]=0;
		currentParams->moveA[i]=0;
		currentParams->topA[i]=0;
	}
	currentParams->dim1 = malloc(sizeof(int) * nnz);
	currentParams->dim2 = malloc(sizeof(int) * nnz);
	currentParams->dim1A = malloc(sizeof(int) * N * 10);
	currentParams->dim2A = malloc(sizeof(int) * N * 10);
	currentParams->b = malloc(N*sizeof(double));
	currentParams->expoArray = malloc(N*sizeof(double));	
	currentParams->st = malloc(N*sizeof(double));	
	currentParams->paramVector = gsl_vector_alloc(5);
	gsl_vector_set_all(currentParams->paramVector,1.0);
	currentParams->fEvals=0;

	
	//set up some petsc matrices
	ierr = MatCreate(PETSC_COMM_WORLD,&currentParams->C);CHKERRQ(ierr);
//	MatSetType(C,MATMPIAIJ);
	ierr = MatSetSizes(currentParams->C, PETSC_DECIDE, PETSC_DECIDE,N,N);CHKERRQ(ierr);
	ierr = MatSetFromOptions(currentParams->C);CHKERRQ(ierr);
//	MatSetOption(currentParams->C, MAT_NEW_NONZERO_ALLOCATION_ERR, PETSC_FALSE);
	ierr = MatSetUp(currentParams->C);CHKERRQ(ierr);
	ierr = MatCreateDense(PETSC_COMM_WORLD,PETSC_DECIDE, PETSC_DECIDE, N, N, NULL, &currentParams->denseMat1);CHKERRQ(ierr);
	
	ierr = MFNCreate(PETSC_COMM_WORLD,&currentParams->mfn);CHKERRQ(ierr);
	
	ierr = MFNSetFunction(currentParams->mfn,SLEPC_FUNCTION_EXP);CHKERRQ(ierr);
	ierr = MFNSetTolerances(currentParams->mfn,1e-07,PETSC_DEFAULT);CHKERRQ(ierr);
	
	
	//mapping stateSpace to reducedSpace
	currentParams->reducedStateSpace = afsStateSpaceNew();
	afsStateSpaceMapAndReducePopn(currentParams->stateSpace, currentParams->map, currentParams->reducedStateSpace, currentParams->reverseMap);
	currentParams->Na=currentParams->reducedStateSpace->nstates;
		
	VecCreate(PETSC_COMM_WORLD,&currentParams->ancStateVec);
	VecSetSizes(currentParams->ancStateVec,PETSC_DECIDE,currentParams->Na);
	VecSetFromOptions(currentParams->ancStateVec);
	VecDuplicate(currentParams->ancStateVec,&currentParams->ancResVec);

	//set up more matrices
	ierr = MatCreate(PETSC_COMM_WORLD,&currentParams->C2);CHKERRQ(ierr);
	MatSetType(currentParams->C2,MATMPIAIJ);
	ierr = MatSetSizes(currentParams->C2, PETSC_DECIDE, PETSC_DECIDE,currentParams->Na,currentParams->Na);CHKERRQ(ierr);
	ierr = MatSetFromOptions(currentParams->C2);CHKERRQ(ierr);
	ierr = MatSetUp(currentParams->C2);CHKERRQ(ierr);

	ierr = MatCreateDense(PETSC_COMM_WORLD,PETSC_DECIDE, PETSC_DECIDE, currentParams->Na, currentParams->Na, NULL, &currentParams->denseMat2);CHKERRQ(ierr);
	// setup done! ///////////////
	////////////////////////////////////

	
	//import mats
	mcMatsImportFromFile(filename2, &nnz, currentParams->top, currentParams->move, currentParams->dim1, currentParams->dim2);
	currentParams->nnz = nnz;
		
	
	if(runMode != 2){
		//alloc data matrix and import data; estimate pi and N1
		currentParams->obsData = gsl_matrix_alloc(n1+1,n2+1);
		import2DSFSData(filename3, currentParams->obsData);
		snpNumber = gsl_matrix_sum(currentParams->obsData);	
		pi_est = 0.0;
		for(i=1;i<n1;i++){
			for(j=0;j<n2+1;j++){
				p =  (float) i / n1;
				pi_est += (2 * p * (1.0 - p)) * gsl_matrix_get(currentParams->obsData,i,j);
			}
		}
		pi_est *= n1 / (n1 - 1) / snpNumber;
		N0=pi_est / u;
	}
	////////////////////////////

	//expected AFS
	switch(runMode){
		
		
		case 1:
		if(rank==0){
			printf("\nParameter estimation run mode\n\n");
			printf("now optimizing....\n\n");
			printf("initial parameter guess:\n");
			for(i=0;i<5;i++)printf("%f ",mle[i]);
			printf("\n\n");
		}
		maximizeLikNLOpt(&lik, currentParams, mle);	
		if(rank == 0){
			printf("Composite Likelihood estimates of IM params (scaled by 1/theta_pop1):\n");
			printf("theta_pop2\ttheta_anc\tmig_1->2\tmig_2->1\tt_div\n");
			for(i=0;i<5;i++)printf("%f\t",(float)mle[i]);
			printf("\n\nComposite Likelihood estimates of IM params (unscaled):\n");
			
			printf("theta_pop2\ttheta_anc\tmig_1->2\tmig_2->1\tt_div\n");
			for(i=0;i<4;i++)printf("%f\t",(float)mle[i]*N0);
			printf("%f\t",(float)mle[i] * N0 * 4.0/ (float)genPerYear);
			printf("\n\nlikelihood: %lf\n",-lik);
			currentParams->nnz = nnz;
			printf("\nExpected AFS:\n");
			gsl_matrix_prettyPrint(currentParams->expAFS);
		}
		
		
		break;	
		case 2:
		if(rank == 0)printf("expected value run mode\n");
		for(i=0;i<5;i++){
			gsl_vector_set(currentParams->paramVector,i,mle[i]);
		}
		calcLogAFS_IM(currentParams);
		currentParams->nnz = nnz;
		if(rank == 0){
			printf("Expected AFS:\n");
			gsl_matrix_prettyPrint(currentParams->expAFS);
			printf("parameter values used:\n");
			for(i=0;i<dim;i++)printf("%f\t",gsl_vector_get(currentParams->paramVector,i));
			printf("\n\n");
		}
		break;
		case 3:
		if(rank==0){
			printf("\nParameter estimation run mode with multiple optimizations\n\n");
			printf("now optimizing....\n\n");
			printf("initial parameter guess:\n");
			for(i=0;i<5;i++)printf("%f ",mle[i]);
			printf("\n\n");
		}
		for(j = 0; j < 3; j++){
			
			for(i=0;i<5;i++)mle[i] = gsl_ran_flat(r,lowerBounds[i], upperBounds[i]);
			printf("optimization %d initial parameter guess:\n",j);
			for(i=0;i<5;i++)printf("%f ",mle[i]);
			maximizeLikNLOpt(&lik, currentParams, mle);	
			if(rank == 0){
				printf("Composite Likelihood estimates of IM params (scaled by 1/theta_pop1):\n");
				printf("theta_pop2\ttheta_anc\tmig_1->2\tmig_2->1\tt_div\n");
				for(i=0;i<5;i++)printf("%f\t",(float)mle[i]);
				printf("\n\nlikelihood: %lf\n",-lik);
				currentParams->nnz = nnz;
				printf("\nExpected AFS:\n");
				gsl_matrix_prettyPrint(currentParams->expAFS);
			}
		}
		
		break;
		case 4:
		if(rank==0){
			printf("\nParameter estimation run mode\n\n");
			printf("now optimizing....\n\n");
			printf("initial parameter guess:\n");
			for(i=0;i<5;i++)printf("%f ",mle[i]);
			printf("\n\n");
		}
		maximizeLikNLOpt_MLSL(&lik, currentParams, mle);	
		if(rank == 0){
			printf("Composite Likelihood estimates of IM params (scaled by 1/theta_pop1):\n");
			printf("theta_pop2\ttheta_anc\tmig_1->2\tmig_2->1\tt_div\n");
			for(i=0;i<5;i++)printf("%f\t",(float)mle[i]);
			printf("\n\nComposite Likelihood estimates of IM params (unscaled):\n");
			
			printf("theta_pop2\ttheta_anc\tmig_1->2\tmig_2->1\tt_div\n");
			for(i=0;i<4;i++)printf("%f\t",(float)mle[i]*N0);
			printf("%f\t",(float)mle[i] * N0 * 4.0/ genPerYear);
			
			printf("\n\nlikelihood: %lf\n",-lik);
			currentParams->nnz = nnz;
			printf("\nExpected AFS:\n");
			gsl_matrix_prettyPrint(currentParams->expAFS);
		}
		
		
		break;
		case 5:
		if(rank==0){
			printf("\nUncertainty estimation (via Fisher Information) run mode\n\n");
			printf("MLE parameters:\n");
			for(i=0;i<5;i++)printf("%f ",mle[i]);
		}
		lik = calcLikNLOpt(5,mle,NULL,currentParams);
		fi = getFisherInfoMatrix(mle, lik, currentParams);
		if(rank==0){
			printf("\nlikelihood: %lf\n",-lik);
			printf("Composite Likelihood estimates of IM params (scaled by 1/theta_pop1):\n");
			printf("theta_pop2\ttheta_anc\tmig_1->2\tmig_2->1\tt_div\n");
			for(i=0;i<5;i++)printf("%f\t",(float)mle[i]);
			printf("\n\nComposite Likelihood estimates of IM params (unscaled):\n");
			printf("theta_pop2\ttheta_anc\tmig_1->2\tmig_2->1\tt_div\n");
			for(i=0;i<4;i++)printf("%f\t",(float)mle[i]*N0);
			printf("%f\t",(float)mle[i] * N0 * 4.0/ (float)genPerYear);
			printf("Uncertainty estimates of IM params (scaled by 1/theta_pop1):\n");
			printf("theta_pop2\ttheta_anc\tmig_1->2\tmig_2->1\tt_div\n");
			for(i=0;i<5;i++)printf("%f\t",(float) sqrt(gsl_matrix_get(fi,i,i)));
			printf("\n\nUncertainty estimates of IM params (unscaled):\n");
			printf("theta_pop2\ttheta_anc\tmig_1->2\tmig_2->1\tt_div\n");
			for(i=0;i<4;i++)printf("%f\t",(float) sqrt(gsl_matrix_get(fi,i,i))*N0);
			printf("%f\t",(float)sqrt(gsl_matrix_get(fi,i,i)) * N0 * 4.0/ (float)genPerYear);
		}
		break;
	}
		
	if(obsFlag && rank==0){
		gsl_matrix_scale(currentParams->obsData,1.0/snpNumber);
		printf("%f SNPs in dataset\n",snpNumber);
		printf("Observed AFS:\n");
		gsl_matrix_prettyPrint(currentParams->obsData);
		gsl_matrix_scale(currentParams->obsData,snpNumber);
	}
	
	
	MatDestroy(&currentParams->denseMat1);
	MatDestroy(&currentParams->denseMat2);
	MatDestroy(&currentParams->C);
	MatDestroy(&currentParams->C2);
	MFNDestroy(&currentParams->mfn);
	VecDestroy(&currentParams->ancStateVec);
	VecDestroy(&currentParams->ancResVec);
	gsl_rng_free (r);
	
	time2=clock();
	if(rank==0)printf("total run time:%f secs\n Liklihood Func. Evals: %d\n",(double) (time2-time1)/CLOCKS_PER_SEC,currentParams->fEvals);
	ierr = PetscFinalize();

	return(0);
}

//import2DSFSData -- imports a matrix from fileName and stuffs it in prealloc'd obsData
void import2DSFSData(const char *fileName, gsl_matrix *obsData){
	FILE *infile;
	
	//open file
	infile = fopen(fileName, "r");
	if (infile == NULL){
		fprintf(stderr,"Error opening data file! ARRRRR!!!!\n");
		exit(1);
	}
	
	gsl_matrix_fscanf(infile,obsData);
	fclose(infile);
}
