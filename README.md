# MAC0352---EP1

Esta tarefa trabalha com programação com soquetes e implementa um protocolo
entre cliente e servidor TCP. Se refere ao curso de Redes de Computadores e Sistemas Distribuídos,
oferecido em 2026 no IME-USP.

O relatório do que foi feito está em 'relatorio - Serviço de Coordenação Distribuída Com Soquetes TCP.pdf',
enquanto a apresentação de slides em 'Sistema Cliente-Servidor.pdf'

## Compilação e Execução
Compilar o servidor: gcc -o WebServer WebServer.c -lpthread
Compilar o cliente: gcc -o client WebClient.c

Se preferir, use para compilar tudo:

```
make
```

e para apagar os executáveis:
```
make clean 
```

Executar o servidor: ./WebServer
Executar o cliente: ./WebClient localhost
Comunicar-se: digitar comandos como CREATE cpu 100, LIST, RESERVE cpu, EXIT, descritos no relatório desta tarefa.
