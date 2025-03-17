#! /bin/bash

if ! [[ -x lsh ]]; then
    echo "lsh executable does not exist"
    exit 1
fi

./run-tests.sh $*


