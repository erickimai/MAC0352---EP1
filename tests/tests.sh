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
    # Cria arquivo temporário com comandos do cliente escritor
    tmp=$(mktemp)
    echo "CREATE cachorro 1" >> "$tmp"
    echo "RESERVE cachorro" >> "$tmp"
    echo "CREATE gato 2" >> "$tmp"
    echo "RESERVE gato" >> "$tmp"

    (cat "$tmp"; sleep 2) | ../WebClient localhost &
    WRITER_PID=$!

    sleep 2

    # Mata abruptamente — sem EXIT, força auto-release no servidor
    kill -9 $WRITER_PID
    wait $WRITER_PID 2>/dev/null

    sleep 1  # dá tempo do servidor processar a desconexão

    # Cliente observador tenta reservar os recursos liberados
    {
        echo "LIST"
        echo "RESERVE cachorro"
        sleep 1
        echo "RESERVE gato"
        sleep 1
        echo "LIST"
        sleep 1
        echo "EXIT"
    } | ../WebClient localhost

    rm "$tmp"
    ;;

    # 'Test Set 4: Overload'

    4)
    FIFOS=()
    PIDS=()

    for i in $(seq 1 10); do
        fifo=$(mktemp -u)
        mkfifo "$fifo"
        FIFOS+=("$fifo")
        
        ../WebClient localhost < "$fifo" &
        PIDS+=($!)
        
        {
        sleep 1
        echo "CREATE id${i} ${i}"
        sleep 1
        echo "RESERVE id${i}"
        sleep 1
        echo "LIST"
        sleep 1
        echo "EXIT"
        sleep 1        # mantém o fifo aberto para o recv do EXIT completar
        } > "$fifo" &
    done

    wait "${PIDS[@]}"  # espera os WebClients

    for fifo in "${FIFOS[@]}"; do
        rm -f "$fifo"
    done
    ;;

    *)
    echo "invalid input"
    ;;

esac

kill -9 $SERVER_PID
