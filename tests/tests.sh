#!/bin/sh

../WebServer & 
SERVER_PID=$!

sleep 1 

{
    echo "CREATE cpu 3"
    sleep 1
    echo "GET cpu"
    sleep 1
    echo "LIST"
    sleep 1
    echo "CREATE memory 100"
    sleep 1
    echo "RESERVE memory"
    sleep 1
    echo "GET memory"
    sleep 1
    echo "RESERVE cpu"
    sleep 1
    echo "SET cpu 5"
    sleep 1
    echo "GET cpu"
    sleep 1
    echo "LIST"
    sleep 1
    echo "RELEASE memory"
    sleep 1
    echo "EXIT"
} | ../WebClient localhost 

kill $SERVER_PID
