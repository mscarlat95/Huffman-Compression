#!/bin/bash

inputFile="README_L"
outputFile="out"

module load compilers/solarisstudio-12.5
time collect ./huffcode -i "$inputFile" -o "$outputFile" -c
exit 0
