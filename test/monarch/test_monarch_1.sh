#!/bin/bash

# exit on error
set -e
# turn on command echoing
set -v
# make sure that the current directory is the one where this script is
cd ${0%/*}
# make the output directory if it doesn't exist
mkdir -p out

((counter = 1))
while [ true ]
do
  echo Attempt $counter

if [[ $1 == "MPI" ]]; then
  exec_str="mpirun -v -np 2 ../../mock_monarch config_simple.json interface_simple.json out/simple"
else
  if [ -z ${SLURM_TASK_PID+x} ]; then
    exec_str="../../mock_monarch config_simple.json interface_simple.json out/simple"
  else
    exec_str="mpirun -v -np 1 --bind-to none  ../../mock_monarch config_simple.json interface_simple.json out/simple"
  fi
fi

  if ! $exec_str; then
	  echo Failure "$counter"
	  if [ "$counter" -gt 0 ]
	  then
		  echo FAIL
		  exit 1
	  fi
	  echo retrying...
  else
	  echo PASS
	  exit 0
  fi
  ((counter++))
done

