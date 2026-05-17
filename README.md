# SeedShell 🌱
A custom Unix shell built from scratch in C.

## Features
- Process management: fork, exec, wait
- IPC via pipes: pipe() and dup2()
- Signal handling: Ctrl+C kills child, not shell
- Multithreading: pthreads with mutex (seedrunthread)
- Race condition demo (seedrace)
- I/O redirection: >, >>, 

## How to compile and run
gcc seedshell.c -o seedshell -lpthread
./seedshell

## Commands
Type seedhelp inside the shell to see all commands.
