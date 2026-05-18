## A simple loopback character device driver that can operate in two modes, controllable by ioctl. 
### The first mode is single byte mode and always reads out the last byte written in. 
### The second mode is buffered, and will read out the last write within a certain size (e.g. 8192). 
### The buffered mode is similar to a pipe, but global to all processes instead of to a file descriptor pair.