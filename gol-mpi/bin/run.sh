#!/bin/bash
salloc -N $1 /usr/lib64/openmpi/bin/mpirun --map-by ppr:1:node ./gol_mpi
