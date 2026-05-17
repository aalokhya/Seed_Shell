# 🌱 SeedShell — Custom Unix Shell in C

A fully functional Unix shell built from scratch in C, implementing core OS concepts including process management, inter-process communication, signal handling, and multithreading.


## 🚀 Features

- **Process Management** — fork, exec, wait system calls
- **IPC via Pipes** — pipe() and dup2() — supports cmd1 | cmd2 | cmd3
- **Signal Handling** — Ctrl+C kills child process, shell stays alive
- **Multithreading** — pthreads with mutex lock (seedrunthread)
- **Race Condition Demo** — seedrace shows data loss without mutex
- **I/O Redirection** — supports >, >>, 


## 📋 Commands

| Command | What it does |
|---|---|
| seedhelp | List all commands |
| seedpid | Show shell process ID |
| seedtime | Current date and time |
| seedlist | List files |
| seedmake | Create directory |
| seedwho | Current user |
| seedwait | Pause execution |
| seedadd | Add two numbers |
| seedpipe | IPC pipe demo |
| seedrunthread | Multithreading with mutex |
| seedrace | Race condition demo |


## 📸 Screenshots


![SeedShell Running](1%20(2).png)


![Commands Demo](2%20(1).png)


![Process Management](2%20(2).png)


![IPC and Signals](2%20(3).png)


![Multithreading Demo](2%20(4).png)


![Signal Handling](2%20(5).png)


## 🧠 OS Concepts Covered

- Process creation and lifecycle
- Inter-process communication
- Signal handling
- Multi-threading and synchronisation
- File descriptor manipulation
- File system operations

