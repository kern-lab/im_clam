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
make
```

