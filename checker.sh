#/!bin/bash

path=`pwd`

echo -e "---------------\tSerial\t---------------"
cd serial
make clean && make
time ./huffcode -i inputFile -o serial_out -c


echo -e "\n---------------\tParallel OMP\t---------------"
cd "$path"/parallel/omp
make clean && make
time ./huffcode -i inputFile -o omp_out -c

echo -e "\n---------------\tParallel PTHREADS\t---------------"
cd "$path"/parallel/pthreads
make clean && make
time ./huffcode -i inputFile -o pthreads_out -c

echo -e "\n---------------\tParallel MPI\t---------------"
cd "$path"/parallel/mpi
make clean && make
time ./huffcode -i inputFile -o mpi_out -c


echo -e "\n\n---------------\tChecker\t---------------"
echo -n -e "1) Serial VS Parallel OMP:\t"
(diff "$path"/serial/serial_out "$path"/parallel/omp/omp_out && echo "Succeeded" ) || echo "Failed" 

echo -n -e "1) Serial VS Parallel PTHREADS:\t"
(diff "$path"/serial/serial_out "$path"/parallel/omp/pthreads_out && echo "Succeeded" ) || echo "Failed" 


echo -n -e "1) Serial VS Parallel MPI:\t"
(diff "$path"/serial/serial_out "$path"/parallel/omp/mpi_out && echo "Succeeded" ) || echo "Failed" 
