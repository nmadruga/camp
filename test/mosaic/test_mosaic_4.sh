#!/bin/bash

# exit on error
set -e
# turn on command echoing
set -v
# make sure that the current directory is the one where this script is
cd ${0%/*}

((counter = 1))
while [ true ]
do
  echo Attempt $counter

if ! ../../camp run_part_restarted.spec || \
   ! ../../extract_aero_time out/mosaic_restarted_0001 || \
   ! tail -n +13 out/mosaic_0001_aero_time.txt > out/mosaic_0001_aero_time_tail.txt || \
   ! ../../numeric_diff --by elem --min-col 2 --rel-tol 1e-4 out/mosaic_0001_aero_time_tail.txt out/mosaic_restarted_0001_aero_time.txt; then
	  echo Failure "$counter"
	  if [ "$counter" -gt 10 ]
	  then
		  echo FAIL
		  exit 1
	  fi
	  echo retrying...
	  if ! ../../camp run_part.spec; then continue; fi
  else
	  echo PASS
	  exit 0
  fi
  ((counter++))
done
