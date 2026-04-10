#!/bin/sh

touch WebServer.log WebClient.log
# Tem que permitir a escrita nos logs
chmod 666 WebServer.log
chmod 666 WebClient.log

read -p "Enter tests: (1 - simple tests; 2 - concurrence between clients; \
3 - failure or auto-release; 4 - overload of clients)" test_set

../WebServer & 
SERVER_PID=$!

sleep 1 

case $test_set in
    # 'Test Set 1: Features'

    1) 
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
    ;;

    # 'Test Set 2: Concurrence'

    2)
    {
        echo "CREATE memory 99"
        echo "RESERVE memory"
        echo "SET memory 100"
        echo "EXIT"
    } | ../WebClient localhost &

    {
        echo "CREATE memory 99"
        echo "RESERVE memory"
        echo "SET memory 100"
        echo "EXIT"
    } | ../WebClient localhost

    ;;

    # 'Test Set 3: Failure'

    3)
    {
        echo "not implemented yet"
        echo "EXIT"
    } | ../WebClient localhost
    ;;

    # 'Test Set 4: Overload'

    4)
    {
        echo "not implemented yet"
        echo "EXIT"
    } | ../WebClient localhost
    ;;

    *)
    echo "invalid input"
    ;;

esac

kill $SERVER_PID
