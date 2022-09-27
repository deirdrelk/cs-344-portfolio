/**
 * CS 344: Smallsh.c Portfolio Project
 * Author: Deirdre Lyons-Keefe
 * Contents: This program runs a small bash type shell. 
 * It has three built in commends: exit, cd, and status. 
 * It forks child processes and runs execvp() on other commands. 
 * It handles input/output redirection and manages background/foreground processes.
 ** */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h> 
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>

// Program specifications: cmd_line input of 2048 chars, 512 args max
#define MAX_IN_LEN 2048
#define MAX_ARG_LEN 512

// GLOBAL VARIABLES
int running = 1;          // Used to keep while loop in Main running; valid termination changes flag
int exit_status;          // Exit status of processes
char * in_file;           // Used when reading args and < is detected
char * out_file;          // Used when reading args and > is detected
int background_flag = 0;  // Background process flag initialized to off
int output_status = 0;    // Flag detecting output redirection 1 is on, 0 is off
int input_status = 0;     // Flag detecting input redirection 1 is on, 0 is off
int bg_pids[256];         // Array for tracking background processes
int bg_index = 0;         // Global index variable for adding and removing from bg_pids

struct sigaction SIGINT_action = {{0}}; 
struct sigaction SIGTSTP_action = {{0}}; 
volatile sig_atomic_t fg_mode = 0; // Used to control foreground mode, initialized to off

// expand_user_input()
// Function that receives three parameters: original string (user input), old string ($$), new string (process id)
// Returns result string
// Reference: https://www.geeksforgeeks.org/c-program-replace-word-text-another-given-word 
char *
expand_user_input(const char * original_string, const char * old_string, const char * new_string) 
{
  // initialize variables
  char * result;
  int index = 0;
  int old_str_len = strlen(old_string);
  int new_str_len = strlen(new_string);
  // malloc result string
  result = (char*)malloc(MAX_IN_LEN);
  // iterate through list
  while (*original_string) 
  {
    // compare the substring with the result and copy as needed
    if (strstr(original_string, old_string) == original_string) 
    {
      // if they're equal, copy the results with new string
      strcpy(&result[index], new_string);
      index += new_str_len;
      original_string += old_str_len;
    }
    else 
    {
      // otherwise, just copy over the original string into result
      result[index++] = *original_string++;
    }
  }
  result[index] = '\0';
  return result;
}

// parse_input()
// Function that parses user input into command line input and argument structs. Returns new cl_input struct.
// Reference for array: https://canvas.oregonstate.edu/courses/1909721/pages/exploration-arrays-and-structures 
// Reference for strtok_r(): https://canvas.oregonstate.edu/courses/1909721/pages/exploration-strings 
char ** 
parse_input(char * user_input) 
{
  // Initialize new vars
  // array of arrays of strings (an array with all arguments in it)
  char ** args = malloc(MAX_ARG_LEN * sizeof(char*));
  // token to read into with strtok_r
  char * token;
  // index in args[] array
  int index = 0;
  // reset input/output flags
  input_status = 0;
  output_status = 0;
  // now keep reading until you've reached the end of the user_input
  // if it's < or > we need to strtok again and save the next token to in/outfiles
  // if it's & the previous process needs to be run in the background
  // if it's none of those, it's an arg and we should add the new argument
  
  // strtok and strtok have to be called once outside the loop; uses NULL after first call
  token = strtok(user_input, " \n");
  while (token != NULL) 
  {
    if (strcmp(token, "<") == 0) 
    {
      // read next token and set it to in_file
      in_file = strtok(NULL, " \n");
      // redirect input status
      input_status = 1;
      // move to the next token (like final else condition below)
      token = strtok(NULL, " \n");
      // nullify args position, so we don't try and read < as a filename
      args[index] = NULL;
      index++;
      continue;
    }
    else if (strcmp(token, ">") == 0)
    {
      // read next token and set it to out_file
      out_file = strtok(NULL, " \n");
      // redirect output status 
      output_status = 1;
      // move to the next token
      token = strtok(NULL, " \n");
      // nullify args position so we don't try and write to >
      args[index] = NULL;
      index ++;
      continue;
    }
    else if (strcmp(token, "&") == 0)
    {
      // move to next position then
      // check if end of user_input and if so, enable BG flags
      token = strtok(NULL, " \n");
      args[index] = NULL;
      index ++;
      if (token == NULL)
      {
        // if foreground mode is enabled, turn BG flag off
        if (fg_mode) 
        {
          background_flag = 0;
        }
        // otherwise, run process in the background
        else 
        {
          background_flag = 1;
        }
        args[index] = NULL;
        break;
      }
      token = strtok(NULL, " \n");
    } 
    else 
    {
      // doesn't match the chars to watch out for, so add to args
      args[index] = token;
      index++;
      token = strtok(NULL, " \n");
    }
  }
  // set final index in args to NULL 
  args[index] = NULL;
  return args;
}

// TSTP_handler()
// Function handler for SIGTSTP_action to control FG/BG mode
// Handles switching foreground_flag: if 0, activate foreground mode
// if 1, exit foreground mode
void
TSTP_handler(int signo)
{
  if (fg_mode == 0) 
  {
    char * message = "\nEntering foreground-only mode (& is now ignored)\n";
    write(1, message, 50);
    fg_mode = 1;
  }
  else
  {
    char * message = "\nExiting foreground-only mode\n";
    write(1, message, 30);
    fg_mode = 0;
  }
  write(1, ": ", 2);
}

// expand_SIGINT_action()
// Function to initialize SIGINT struct 
// Resource Module: Signal handling API
// Resource: Benjamin Brewster lecture: https://www.youtube.com/watch?v=VwS3dx3uyiQ 
void 
expand_SIGINT_action()
{
  // Set handler to ignore SIGINT by default
  SIGINT_action.sa_handler = SIG_IGN;
  // Block all catchable signals while masked
  sigfillset(&SIGINT_action.sa_mask);
  SIGINT_action.sa_flags = 0;
  // Register the functionality of SIGINT
  sigaction(SIGINT, &SIGINT_action, NULL);
}

// expandSIGTSTP_action()
// Function to set SIGTSTP struct
// Resource Module: Signal handling API
// Resource: Benjamin Brewster lecture: https://www.youtube.com/watch?v=VwS3dx3uyiQ
void 
expand_SIGTSTP_action()
{
  // Specify the signal handler function
  SIGTSTP_action.sa_handler = &TSTP_handler;
  // Block all catchable signals while masked
  sigfillset(&SIGTSTP_action.sa_mask);
  // Set flag to restart 
  SIGTSTP_action.sa_flags = SA_RESTART;
  // Register the functionality of SIGTSTP
  sigaction(SIGTSTP, &SIGTSTP_action, NULL);
}

// fork_cmd() 
// Function that processes built in shell commands
// Parent forks a child, and then waits for the termination of the child 
// Resource Modules: Executing a new program, I/O redirection
// Resource: Benjamin Brewster lectures: https://www.youtube.com/watch?v=1R9h-H2UnLs | https://www.youtube.com/watch?v=kx60fayG-qY 
int
fork_cmd(char ** args) 
{
  // whenever a non-built in command is received, fork off a child
  pid_t spawn_pid = fork();
  pid_t wait_pid;

  // create mask to keep FG functions from being interrupted by SIGTSTP
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGTSTP);

  switch(spawn_pid) 
  {
    // error handling
    case -1: 
      perror("fork() failed\n");
      exit(1);
      break;
    // starting a child process
    case 0:
      // if input redirection is on
      if (input_status) 
      {
        // get read file descriptor
        int read_fd = open(in_file, O_RDONLY);
        if (read_fd == -1) 
        {
          fprintf(stderr, "Cannot open input file: %s\n", in_file);
          fflush(stderr);
          exit(1);
        }
        else 
        {
          // use dup2 to redirect file descriptor to stdin
          if (dup2(read_fd, 0) == -1) 
            {
              perror("dup2() failed");
            } 
          close(read_fd);
        }
      }
      // if output redirection is on
      if (output_status) 
      {
        // get write file descriptor
        int write_fd = open(out_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (write_fd == -1) 
        {
          fprintf(stderr, "Cannot create file for output: %s\n", out_file);
          fflush(stderr);
          exit(1);
        }
        else 
        {
          // use dup2 to redirect write file to stdout
          if (dup2(write_fd, 1) == -1) 
          {
            perror("dup2() failed\n");
          }
          close(write_fd);
        }
      }
      if (background_flag)
      // if it's a background process, check if input needs to be redirected to /dev/null
      {
        if (!input_status)
        {
          int read_fd = open("/dev/null", O_RDONLY);
          if (read_fd == -1)
          {
            fprintf(stderr, "Cannot redirect read_fd\n");
            fflush(stderr);
            exit(1);
          }
          else
          {
            if (dup2(read_fd, 0) == -1) 
            {
              perror("dup2() failed");
            }
            close(read_fd);
          }
        }
        if (!output_status)
        {
          int write_fd = open("/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0644);
          if (write_fd == -1)
          {
            fprintf(stderr, "Cannot redirect write_fd\n");
            fflush(stderr);
            exit(1);
          }
          else
          {
            if (dup2(write_fd, 1) == -1)
            {
              perror("dup2() failed");
            }
          }
          close(write_fd);
        }
      }
      if (!background_flag) 
      {
        // if background is off, SIGINT action should be default for foreground processes
        SIGINT_action.sa_handler = SIG_DFL;
        SIGINT_action.sa_flags = 0;
        sigaction(SIGINT, &SIGINT_action, NULL);
      }
      // now time to execute the argument
      if (execvp(args[0], args) == -1)
      {
        fprintf(stderr, "Value of errno: %d\n", errno);
        fflush(stderr);
        perror("error");
        exit(1);
      }
      break;
    // parent case
    default:
      // Waiting for child process to terminate and updating exit_status value 
      if (!background_flag || fg_mode) 
      {
        wait_pid = waitpid(spawn_pid, &exit_status, 0);
        if (wait_pid == -1)
        {
          perror("wait_pid");
          exit(1);
        }
        if (WIFSIGNALED(exit_status))
        {
          fprintf(stdout, "exit: %d\n", WTERMSIG(exit_status));
          fflush(stdout);
          exit_status = WTERMSIG(exit_status);
        }
        else
        {
          exit_status = WEXITSTATUS(exit_status);
        }
      }
      // Background child process = store pid in bg_pids[] array, and reset background flag
      else
      {
        fprintf(stdout, "Background pid is %d\n", spawn_pid);
        fflush(stdout);
        bg_pids[bg_index] = spawn_pid;
        bg_index++;
        background_flag = 0;
      }
  }
  return 0;
}

// execute_cmd() 
// Function that executes the three built in commands if args[0] = exit, cd or status.
// If args[0] does not match, it calls helper function fork_cmd(args) to fork and execute child processes
void
execute_cmd(char ** args) {
  // If command is 'exit' terminate all background processes
  // and deactivate shell by setting running flag to FALSE/0
  if (strcmp(args[0], "exit") == 0) 
  {
    // kill all bg processes still running.
    while (bg_index > 0) 
    {
      kill(bg_pids[0], SIGKILL);
      // shift elements to remove from bg_pids array
      for (int index = 0; index < bg_index - 1; index++) 
      {
        bg_pids[index] = bg_pids[index + 1];
      }
      bg_index--;
    }
    running = 0;
  }
  // If command is 'cd' change the directory to arg string provided
  // handle errors/failed directories
  // and check for "home" by seeing if next element is NULL (aka end of args)
  else if (strcmp(args[0], "cd") == 0)
  {
    char cwd[256];
    getcwd(cwd, 256);    
    if(args[1] == NULL) 
    {
      // this means we need to change to the
      // directory specified in the HOME environment variable
      if(chdir(getenv("HOME")) != 0)
      {
        perror("chdir failed");
      }
    }
    else
    { 
      if(chdir(args[1]) != 0) 
      {
        perror("chdir failed");
      }
    }
  }
  // if command is 'status' print out the exit status or the terminating signal of the last foreground
  // process run by the shell. If it runs before any foreground command is run, it returns exit status 0
  else if (strcmp(args[0], "status") == 0) 
  {
    // handle background case - turn flag off if on
    if (background_flag) 
    {
      background_flag = 0;
    }
    //get appropriate status and print as needed [from exploration Process API - Monitoring Child Processes] 
    // If child terminated normally 
    if (WIFEXITED(exit_status)) 
    {
      
      fprintf(stdout, "exit value: %d\n", WEXITSTATUS(exit_status));
      fflush(stdout);
    }
    // If child terminated abnormally
    else
    {
      fprintf(stdout, "exit value: %d\n", WTERMSIG(exit_status));
      fflush(stdout);
    }
  }
  else 
  {
    fork_cmd(args);
  }
}

// check_background_processes()
// This method checks for any background processes that have ended.
// If the bg_index is greater than 0, there is a process in bg_pids array, so
// Check for termination or exit of process, and print pid along with exit status
// Remove from bg_pids array
void
check_bg_processes() 
{
  int index;
  pid_t bg_pid = waitpid(-1, &exit_status, WNOHANG);
  while (bg_pid > 0) 
  {
    for (index = 0; index < bg_index; index++)
    {
      if (bg_pids[index] == bg_pid)
        while (index < bg_index - 1) 
        {
          bg_pids[index] = bg_pids[index + 1];
        }
        bg_index--;
        break;
    }
    if (WIFEXITED(exit_status))
    {
      fprintf(stdout, "Background pid %d is finished. Exit value %d\n", bg_pid, WEXITSTATUS(exit_status));
      fflush(stdout);
    }
    else if (WIFSIGNALED(exit_status))
    {
      fprintf(stdout, "Background pid %d is finished. Terminated by signal %d\n", bg_pid, WTERMSIG(exit_status));
      fflush(stdout);
    }
    bg_pid = waitpid(-1, &exit_status, WNOHANG);
  }
}

// main()
// Initializes default SIGINT and SIGTSTP structs.
// Checks for all bg processes that have ended - and prints any that have, along with why.
// Receives user input continuously until running flag is set to 0. 
// Validates input [ignores newline and # chars] and expands input if $$ is detected.
// Then parses that input to the command struct.
// Finally it executes the command, either as built in commands [cd, status, exit] or via execvp()
int
main(void) 
{
  // Initialize sigaction structs to default behavior
  expand_SIGINT_action();
  expand_SIGTSTP_action();
  // Receive user input until running flag is set to 0
  while (running) 
  {
    // loop through bg_pids checking for exited processes to print and remove from array
    if (bg_index > 0) 
    { 
      check_bg_processes();      
    }    
    // read input
    fflush(stdout);
    char user_input[MAX_IN_LEN];
    char ** commands;
    printf(": ");
    fflush(stdout);
    fgets(user_input, MAX_IN_LEN, stdin);
    // ignore user input if it's a comment or a new line
    if (user_input[0] != '#' && strcmp(user_input, "\n") != 0) 
    {
      // check and see if you received a $$ in there and need to expand the pid
      // use strstr to find first occurance, indicating the helper function needs to be called
      char * pid_check = strstr(user_input, "$$"); 
      // if $$ found then get pid, and replace $$ with pid in input
      if (pid_check) 
      {
        char * expanded_input = NULL;
        char pid_str[6];
        sprintf(pid_str, "%d", getpid());
        expanded_input = expand_user_input(user_input, "$$", pid_str);
        commands = parse_input(expanded_input);  
      }
      // otherwise, just parse input as usual
      else
      {
        commands = parse_input(user_input);
      }
      // so we can execute commands
      execute_cmd(commands);
      
      // finally, free memory
      free(commands);
      in_file = NULL;
      out_file = NULL;
     }
  }
  return 0;
}
