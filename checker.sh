#/!bin/bash



function run() {
	cd "$1"
	make clean && make
	time ./huffcode -i inputFile -o "$2" -c
}

function compare() {
	echo -n -e "$1" # test case

	(diff "$2" "$3"  && echo "Succeeded" ) || echo "Failed" 
}

function main() {
	path=`pwd`

	echo -e "---------------\tSerial\t---------------"
	run "$path/serial" "serial_out"

	echo -e "\n---------------\tParallel OMP\t---------------"
	run "$path/parallel/omp" "omp_out"

	echo -e "\n---------------\tParallel PTHREADS\t---------------"
	run "$path/parallel/pthreads" "pthreads_out"

	echo -e "\n---------------\tParallel MPI\t---------------"
	run "$path/parallel/mpi" "mpi_out"


	echo -e "\n\n---------------\tChecker\t---------------"
	compare "1) Serial VS Parallel OMP:\t" "$path/serial/serial_out" "$path/parallel/omp/omp_out"
	compare "2) Serial VS Parallel PTHREADS:\t" "$path/serial/serial_out" "$path/parallel/pthreads/pthreads_out"
	compare "3) Serial VS Parallel MPI:\t" "$path/serial/serial_out" "$path/parallel/mpi/mpi_out"
}

main 