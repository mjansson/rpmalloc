#!/bin/sh

os="macosx"

for name in rpmalloc tcmalloc crt ptmalloc3 hoard nedmalloc; do
	executable=bin/$os/deploy/benchmark-$name

	$executable 1 0 2 16 1000
	$executable 2 0 2 16 1000
	$executable 3 0 2 16 1000
	$executable 4 0 2 16 1000
	$executable 5 0 2 16 1000
	$executable 6 0 2 16 1000

	$executable 1 0 2 16 8000
	$executable 2 0 2 16 8000
	$executable 3 0 2 16 8000
	$executable 4 0 2 16 8000
	$executable 5 0 2 16 8000
	$executable 6 0 2 16 8000

	$executable 1 0 2 16 16000
	$executable 2 0 2 16 16000
	$executable 3 0 2 16 16000
	$executable 4 0 2 16 16000
	$executable 5 0 2 16 16000
	$executable 6 0 2 16 16000

done
