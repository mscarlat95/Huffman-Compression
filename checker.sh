#/!bin/bash

# run(directory_path, outputFile)
function run() {
	cd "$1"
	make clean && make
	time ./huffcode -i "$2" -o "$3" -c
	make clean
}

# compare (test_description, file1, file2)
function compare() {
	echo -n -e "$1"
	(diff "$2" "$3"  && echo "Succeeded" ) || echo "Failed" 
}

path=`pwd`
function serial() {
	echo -e "---------------\tSerial\t---------------"
	run "$path/serial" "$path/inputFile" "serial_out"
}

function omp() {
	echo -e "\n---------------\tParallel OMP\t---------------"
	run "$path/parallel/omp" "$path/inputFile" "omp_out"
}

function pthreads() {
	echo -e "\n---------------\tParallel PTHREADS\t---------------"
	run "$path/parallel/pthreads" "$path/inputFile" "pthreads_out"
}

function mpi() {
	echo -e "\n---------------\tParallel MPI\t---------------"
	run "$path/parallel/mpi" "$path/inputFile" "mpi_out"
}

# main()
function main() {

	# run each test
	serial
	omp
	pthreads
	mpi

	echo -e "\n\n---------------\tChecker\t---------------"
	compare "1) Serial VS Parallel OMP:\t" "$path/serial/serial_out" "$path/parallel/omp/omp_out"
	compare "2) Serial VS Parallel PTHREADS:\t" "$path/serial/serial_out" "$path/parallel/pthreads/pthreads_out"
	compare "3) Serial VS Parallel MPI:\t" "$path/serial/serial_out" "$path/parallel/mpi/mpi_out"
}

if [ $# -eq 1 ]; then
	"$1" 
elif [ $# -eq 0 ]; then
	main
else
	echo "Usage: ./checker.sh [test_case]"
	echo "Received: "$@
fi

exit 0
