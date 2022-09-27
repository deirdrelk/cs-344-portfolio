CS 344: smallsh.c 
Author: Deirdre Lyons-Keefe
Contents: A custom shell written in C. 

To compile: gcc -std=gnu99 -o smallsh smallsh.c 

Executes exit, cd and status commands via built in functions. 
Manages parent and child processes, with support for foreground or background processes. 
Handles input and output direction. 
Contains custom signal handlers for SIGINT and SIGTSTP. 