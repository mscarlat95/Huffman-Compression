#!/bin/bash

if [[ ! $(diff compressed ../../serial/ref_out) ]]; then
	echo "All good"
else
	echo "NOT GOOD!!!"
fi
