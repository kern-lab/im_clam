# im_clam

## Installation

`im_clam` is has several dependencies, thus installation will require install of multiple third party software packages.
Hopefully, with the help of a modern package manager like homebrew or apt-get this won't be too painful though. Here is a 
list of dependencies

- git
- CMake
- atlas (or another BLAS and LAPACK package)
- glib
- pkg-config
- SuiteSparse
- petsc (version 3.5)
- slepc (version 3.5)
- nlopt
- gsl
- a version of MPI 

Please note that newer versions (>=3.6) of the petsc and slepc libraries break `im_clam` due to strange issues with backward compatibility. One day I will get around to changing all this code, but it is not a current priority. One thing that eases the process of installing dependencies is that `petsc` will take care of many of the packages if you tell it to when you configure the install. I'll demonstrate this below.

## Install on Linux
I focus on linux here as most compute clusters suitable for running `im_clam` will, I assume, be using some flavor of linux. Here I go through the steps necessary on a clean Ubuntu 16.04 install. 

First install the basics
```
apt-get install git libglib2.0-dev cmake pkg-config gsl-bin libgsl-dev libnlopt-dev libsuitesparse-dev
```
Once those packages are installed its time to move on to `petsc`. We will configure `petsc` to install `MPICH` as our MPI installation. For the sake of example I will download everything to my home dir

```
cd ~
wget http://ftp.mcs.anl.gov/pub/petsc/release-snapshots/petsc-3.5.4.tar.gz .
tar zxvf petsc-3.5.4.tar.gz .
cd petsc-3.5.4/
./configure --download-fblaslapack --download-mpich
```
this will take a few minutes to run. next you are ready to make `petsc`
```
make PETSC_DIR=/home/username/petsc-3.5.4 PETSC_ARCH=arch-linux2-c-debug all
```
with `username` replaced with the proper value of your home directory or where ever else you have downloaded petsc. once the compilation is complete, you can test it using `make test`. `petsc` relies on two environmental variables, `PETSC_DIR` and `PETSC_ARCH`. Its a good idea to add a couple lines to your .bash_profile such as
```
export PETSC_DIR=/home/username/petsc-3.5.4
export PETSC_ARCH=arch-linux2-c-debug
```
again inserting the proper value of `username`.then source that file
```
source ~/.bash_profile
```
Now we are ready to move on to installing `slepc`. Move to a place where you want to store `slepc`, for me that will be my home directory
```
cd ~
wget http://slepc.upv.es/download/download.php?filename=slepc-3.5.4.tar.gz -O slepc-3.5.4.tar.gz
tar zxvf slepc-3.5.4.tar.gz
cd slepc-3.5.4/
export SLEPC_DIR=/home/username/slepc-3.5.4
./configure
make
make test
```
Its also a good line to add the environmental variable `SLEPC_DIR` to your `.bash_profile` file.

That should be all the dependencies. Now on to `im_clam` itself. Clone the `im_clam` repository to wherever you want it, move into that dir and then lets try make

```
git clone https://github.com/kern-lab/im_clam
cd im_clam
make all
```
currently this runs with a bunch of compiler warnings. Ignore that for now, as I will be cleaning those in the coming weeks, months, years-- everything should run fine.

When compilation is complete you will be left with three programs
- `cmc_stateSpace` - this calculates the state space of an IM model with the specified sample sizes
- `cmc_topol` - this calculates statistics about the eventual transition matrix. 
- `im_clam` - the main program that will compute the expected AFS as well as do inference

## Usage
There are three steps associated with running analyses using `im_clam`. The first two involve calculating the state space for the sample configuration of interest and pre-computing quantities of the transition matrix of the Markov chain. The third step is to run `im_clam` itself with the outputs from the first two steps to either do parameter inference or to calculate the expected AFS from a given model parameterization.

---
### Computing the state space
The first step to using `im_clam` is  calculating the state space for a given sample configuration and its associated topology matrix. This is done using the programs `cmc_stateSpace` and `cmc_topol`. For instance imagine you were interested in a sample of size n=3 from one population and n=4 from a second. To calculate the state space we would use the following call
```
./cmc_stateSpace 3 4 > ss_3_4
```
This saves the state space to the file `ss_3_4`. The output of this program provides states in the form described in the Kern and Hey paper, along with a simple header containing information on the sample sizes specified and the number of associated states. 

Next we calculate the topology matrix
```
./cmc_topol ss_3_4 > ss_3_4_mats
```
The topology matrix output contains one line stating the number of non-zero entries in the transition matrix, followed by a series of lines that information on the topological probability of a move, the move type, and the coordinates of that move in the transition matrix. This output isn't really meant for human consumption, and is done to ease calculations later on. Those two files, `ss_3_4` and `ss_3_4_mats` will be used as input to `im_clam`. I have provided a number of example state space files and their associated topology matrices in the directory `stateSpaceFiles`. Each of these is named after the sample sizes: for instance `stateSpaceFiles/testSS_33` contains the stateSpace for samples of size n1=n2=3, and its associated topology matrix is named `stateSpaceFiles/testSS_33_mats`. These can get you started but you can always calculate your own as shown above.

### Running `im_clam`
With our preliminary files created we are ready to do something useful with them using `im_clam`. A few notes first on this program. `im_clam` makes heavy use of the `petsc` library, a parallel library for numerical calculation. As a result calls to `im_clam` should be done through `mpiexec` or similar (it depends on your MPI installation). `petsc` itself will output a certain amount of information to the screen as the user runs `im_clam`. In large part this can be ignored. Generally runs of `im_clam` for decent sample sizes (say n > 5) should be done on a machine with many cores. I have run `im_clam` on hundreds of cores on a cluster with excellent results in terms of runtime. Your mileage may vary though as `im_clam` is very sensitive to the speed of communication between nodes.

Lets take a quick look at the usage statement from `im_clam`

```
$ ./im_clam 

.___   _____             .__
|   | /     \       ____ |  | _____    _____
|   |/  \ /  \    _/ ___\|  | \__  \  /     \
|   /    Y    \   \  \___|  |__/ __ \|  Y Y  \
|___\____|__  /____\___  >____(____  /__|_|  /
            \/_____/   \/          \/      \/


im_clam -- Isolation with Migration Composite Likelihood Analysis using Markov chains


im_clam
	Example: mpiexec -n <np> ./im_clam -s <stateSpace file> -m <mats file> -d <data file> 

	options:
		-exp expected value mode (requires -x flag too)
		-mo multiple optimizations from different start points
		-global multi-level optimization (MLSL algo.)
		-x <theta_2, theta_A, mig12, mig21, t_div> parameter starting values
		-obs (prints out observed AFS as well as that expected from MLE params)
		-u mutation rate per base pair per generation (only used to unscale parameters)
		-g generation time (gens/year)
		-r randomSeed
		-v verbose output

```
as it says, the proper call is to use mpiexec to then run `im_clam`. There are a few options for use with the program. The first is the run mode. When no options are provided `im_clam` will do parameter estimation using the state space file, topology matrix, and data file specified. Parameter optimization using the low-strage BFGS algorithm. All optimization uses the `nlopt` library and you can read about the optimization algorithms [here](http://ab-initio.mit.edu/wiki/index.php/NLopt_Algorithms). Here is an example

```
$ $PETSC_DIR/arch-linux2-c-debug/bin/mpiexec -n 2 ./im_clam -s stateSpaceFiles/testSS_33 -m stateSpaceFiles/testSS_33_mats -d exampleInput_33 -u 0.00001

.___   _____             .__
|   | /     \       ____ |  | _____    _____
|   |/  \ /  \    _/ ___\|  | \__  \  /     \
|   /    Y    \   \  \___|  |__/ __ \|  Y Y  \
|___\____|__  /____\___  >____(____  /__|_|  /
            \/_____/   \/          \/      \/


im_clam -- Isolation with Migration Composite Likelihood Analysis using Markov chains



Parameter estimation run mode

now optimizing....

initial parameter guess:
7.564897 8.928837 10.018410 2.071886 4.159601 

Composite Likelihood estimates of IM params (scaled by 1/theta_pop1):
theta_pop2	theta_anc	mig_1->2	mig_2->1	t_div
1.013741	0.907719	0.100391	0.098687	2.102067	

Composite Likelihood estimates of IM params (unscaled):
theta_pop2	theta_anc	mig_1->2	mig_2->1	t_div
13222.639092	11839.754624	1309.445195	1287.214853	5483.623451	

likelihood: -125869.001406

Expected AFS:
0.00 0.16 0.09 0.21 
0.16 0.00 0.00 0.02 
0.09 0.00 0.00 0.01 
0.21 0.02 0.01 0.00 
///////////
total run time:14.163778 secs
 Liklihood Func. Evals: 474

```
A few things to note. First I am launching `im_clam` with the version of `MPI` that we bundled with `petsc`. This is an easy way to make sure that things work correctly on the `MPI` side of things so I recommend that approach. Then I have specified my input files along with a single option, the `-u` flag that will allow me to input a mutation rate for unscaled parameter estimates. `im_clam` then outputs the initial parameter starting point, the scaled estimates of the IM parameters, the unscaled parameter estimates (assumes a mutation rate and a generation time), the likelihood, and the expected AFS under the estimated model.

For optimization I provide two other options. The `-mo` flag with run the LS-BFGS three independent times from different starting points. The `-global` flag runs the Multi-Level Single-Linkage algorithm described [here](http://ab-initio.mit.edu/wiki/index.php/NLopt_Algorithms#MLSL_.28Multi-Level_Single-Linkage.29). The starting parameter vector for optimization can be set with the `-x` flag which takes as its argument a comma separated list of parameters in the specified order.

If one uses the `-exp` flag the program does not optimize a model, but instead just calculates the expected AFS under the parameterization specified by the `-x` flag. For instance

 