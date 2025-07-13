#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <termios.h>


#define MAX_LENGTH 1024   /* Maximum length of a command line */
#define MAX_ARGS 64       /* Maximum number of arguments */
#define BUFFER_SIZE 5     /* History buffer size */


/*  Global history buffer and tracking variables */
char history[BUFFER_SIZE][MAX_LENGTH]; /* 2D array, each command has its own line */
int command_count = 0;    /*  Number of commands stored so far (max BUFFER_SIZE) */
int next_command = 0;     /*  Next insertion index (always between 0 and BUFFER_SIZE-1) */
int buffer_index = -1;    /*  Index for browsing history (-1 means not browsing) */


/*  Global variable to hold original terminal settings */
struct termios canonicalSettings;


/*
* Function: restore_canonical_mode
* --------------------------------
* Restores the terminal to its original settings when the program exits.
*/
void restore_canonical_mode() {
   tcsetattr(STDIN_FILENO, TCSAFLUSH, &canonicalSettings);
}


/*
* Function: enable_noncanonical_mode
* ----------------------------------
* Configures the terminal to disable echo and canonical mode for real-time input processing.
*/
void enable_noncanonical_mode() {
   tcgetattr(STDIN_FILENO, &canonicalSettings); /*  Save the current terminal settings to restore later */
   atexit(restore_canonical_mode);              /*  Ensure terminal is restored on exit */
   struct termios noncanonical = canonicalSettings; /*  New termios struct based on the current settings */
   noncanonical.c_lflag &= ~(ECHO | ICANON);    /*  Disable echo and canonical mode */
   tcsetattr(STDIN_FILENO, TCSAFLUSH, &noncanonical); /*  Apply the new settings */
}


/*
* Function: print_prompt
* ----------------------
* Displays the shell prompt with the current working directory.
*/
void print_prompt() {
   char path[1024];
   if (getcwd(path, sizeof(path)) != NULL) {
       char *last_dir = path; /*  Start with full path */
       /*  Special case for root directory */
       if (strcmp(path, "/") == 0) {
           last_dir = "/";
       } else {
           /*  Loop to find the last occurrence of '/' */
           for (int i = 0; path[i] != '\0'; i++) {
               if (path[i] == '/') {
                   last_dir = &path[i + 1]; /*  Point to character after '/' */
                   if (path[i + 1] == '\0') {
                       last_dir = &path[i];
                       break;
                   }
               }
           }
       }
       printf("osc:%s> ", last_dir);
       fflush(stdout);
   } else {
       perror("getcwd() error");
   }
}


/*
* Function: get_input
* -------------------
* Reads user input character by character, handles special keys, and returns the input string.
*/
int get_input(char *buf) {
   int count = 0;  /* character count */
   char c;         /* each individual character */
   memset(buf, 0, MAX_LENGTH);  /* Clear the input buffer */


   /* Simulate 5 up arrow keypresses followed by 5 down arrow keypresses to definitively ensure correct starting position */
   for (int i = 0; i < 5; i++) {
       /* Simulate up arrow keypress */
       if (command_count > 0) {
           int start;
           if (command_count < BUFFER_SIZE) {
               start = 0;
           } else {
               start = next_command;
           }
           int most_recent = (start + command_count - 1) % BUFFER_SIZE;


           if (buffer_index == -1) {
               buffer_index = most_recent;  /* Start at the most recent command */
           } else {
               if (buffer_index == start) {
                   continue;   /* Already at the oldest command */
               } else {
                   buffer_index = (buffer_index - 1 + BUFFER_SIZE) % BUFFER_SIZE;
               }
           }
       }
   }
   for (int i = 0; i < 5; i++) {
       /* Simulate down arrow keypress */
       if (buffer_index != -1) {
           int start;
           if (command_count < BUFFER_SIZE) {
               start = 0;
           } else {
               start = next_command;
           }
           int most_recent = (start + command_count - 1) % BUFFER_SIZE;


           if (buffer_index == most_recent) {
               /* Already at the newest command: reset browsing */
               buffer_index = -1;
           } else {
               buffer_index = (buffer_index + 1) % BUFFER_SIZE;
           }
       }
   }

   /* Infinite loop to read characters one by one */
   while (1) {
       ssize_t n = read(STDIN_FILENO, &c, 1);  /* Read another character from standard input */
       if (n <= 0) /* End of file or error */
           break;


       /* If Enter key is pressed, finish input */
       if (c == '\n' || c == '\r') {
           putchar('\n');  /* New line */
           buf[count] = '\0';  /* Cap off input with null terminator */
           break;
       }
       /* Handle backspace (ASCII DEL 127 or BS 8) */
       else if (c == 127 || c == 8) {
           if (count > 0) {
               count--;
               /* Erase character from terminal: move cursor back, overwrite with space, then move cursor back again */
               printf("\b \b");
               fflush(stdout);
           }
       }
       /* Handle special keys (arrow keys via ESC) */
       else if (c == 27) {
           char seq[2];
           if (read(STDIN_FILENO, &seq[0], 1) == 0)  /* Read first character in sequence (like '[') */
               continue;
           if (read(STDIN_FILENO, &seq[1], 1) == 0)  /* Read second character in sequence (like 'A' or 'B') */
               continue;


           /* Up arrow: ESC [ A */
           if (seq[0] == '[' && seq[1] == 'A') {
               if (command_count > 0) {  /* Edge case check: ensure there is history */
                   /* Find starting position in the history buffer */
                   int start;
                   if (command_count < BUFFER_SIZE) {
                       start = 0;
                   } else {
                       start = next_command;
                   }
                   int most_recent = (start + command_count - 1) % BUFFER_SIZE;


                   if (buffer_index == -1) {
                       buffer_index = most_recent;  /* Start at the most recent command */
                   } else {
                       if (buffer_index == start) {
                           continue;   /* Already at the oldest command */
                       } else {
                           buffer_index = (buffer_index - 1 + BUFFER_SIZE) % BUFFER_SIZE;  /* Move to previous command */
                       }
                   }
                   /* Clear current line and reprint prompt and command */
                   printf("\33[2K\r");
                   print_prompt();  /* Reprint the prompt */
                   /* Copy history command into input buffer and echo it */
                   strcpy(buf, history[buffer_index]);
                   count = strlen(buf);
                   printf("%s", buf);
                   fflush(stdout);
               }
           }
           /* Down arrow: ESC [ B (opposite direction of up arrow) */
           else if (seq[0] == '[' && seq[1] == 'B') {
               if (buffer_index != -1) {
                   /* Find starting position in the history buffer */
                   int start;
                   if (command_count < BUFFER_SIZE) {
                       start = 0;
                   } else {
                       start = next_command;
                   }
                   int most_recent = (start + command_count - 1) % BUFFER_SIZE;


                   if (buffer_index == most_recent) {
                       /* Already at the newest command: reset browsing */
                       buffer_index = -1;
                       count = 0;
                       buf[0] = '\0';
                       printf("\33[2K\r");
                       print_prompt();  /* Reprint the prompt */
                       fflush(stdout);
                       continue;
                   } else {
                       buffer_index = (buffer_index + 1) % BUFFER_SIZE;  /* Move to next command */
                   }
                   /* Clear current line and reprint prompt with the history command */
                   printf("\33[2K\r");
                   print_prompt();
                   /* Load next command from history */
                   strcpy(buf, history[buffer_index]);
                   count = strlen(buf);
                   printf("%s", buf);
                   fflush(stdout);
               }
           }
       }
       else {
           /* For normal characters, add to buffer and echo the character */
           if (count < MAX_LENGTH - 1) {
               buf[count++] = c;
               putchar(c);
               fflush(stdout);
           }
       }
   }
   return count;  /* Return total characters read */
}


/*
* Function: add_to_buffer
* -----------------------
* Stores a new command in the history buffer as long as it doesn't match the most recent, overwriting older commands if necessary
*/
void add_to_buffer(const char *cmd) {
   if (strlen(cmd) == 0)
       return;


   /* If history is not empty, compare with the last command */
   if (command_count > 0) {
       int last_index = (next_command - 1 + BUFFER_SIZE) % BUFFER_SIZE;
       if (strcmp(history[last_index], cmd) == 0) {
           return;  /* Don't add if the new command matches the last one */
       }
   }
 
   strcpy(history[next_command], cmd); /* store in next available slot */
   next_command = (next_command + 1) % BUFFER_SIZE;  /* move index forward and overwrite old ones if needed */
   /* if buffer isnt full yet */
   if (command_count < BUFFER_SIZE)
       command_count++;
 
       buffer_index = -1;  /*  Reset index after adding a new command */
}


/*
* Function: divide_args
* ---------------------
* Splits the input string into an array of arguments and detects background execution (&).
*/
int divide_args(char *input, char *args[], int *background) {
   int i = 0;
   char *segment = strtok(input, " "); /* split input by space */


   while (segment != NULL && i < MAX_ARGS - 1) {
       if (strcmp(segment, "&") == 0) {
           *background = 1;  /*  Mark for background execution if an ampersand is found */
       } else {
           args[i++] = segment;    /* store argument */
       }
       segment = strtok(NULL, " ");    /* get next */
   }
   /* null terminate argument array and return the count */
   args[i] = NULL;
   return i;
}


/*
* Function: handle_cd_and_exit
* ----------------------------
* Implements the cd command to change directories and the exit command to terminate the shell.
*/
int handle_cd_and_exit(char *args[]) {
   /* check for exit command */
   if (strcmp(args[0], "exit") == 0) {
       exit(0);
   }
   /* check for cd command */
   if (strcmp(args[0], "cd") == 0) {
       /* case no directory provided */
       if (args[1] == NULL) {
           fprintf(stderr, "cd: expected argument\n");
       } else {
           /* change directory */
           if (chdir(args[1]) != 0) {
               perror("chdir failed");
           }
       }
       return 1; /* worked */
   }
   return 0; /* didn't work */
}


/*
* Function: handle_pipe
* ---------------------
* Detects and executes commands with a pipe (|).
*/
int handle_pipe(char *args[], int argc) {
   int pipe_index = -1;
   /* scan for | and store index if found */
   for (int j = 0; j < argc; j++) {
       if (strcmp(args[j], "|") == 0) {
           pipe_index = j;
           break;
       }
   }
   /* if pipe found */
   if (pipe_index != -1) {
       args[pipe_index] = NULL;  /*  Terminate left command's argument list */


       int pipe_ends[2];   /* read and write */
       if (pipe(pipe_ends) < 0) {
           perror("pipe failed");
           return 1;
       }


       /*  Fork first child for left-hand command  */
       pid_t pid1 = fork();
       if (pid1 < 0) {
           perror("fork failed");
           return 1;
       } else if (pid1 == 0) {
           /* child process for left hand */
           close(pipe_ends[0]);  /*  Close unused read end */
           if (dup2(pipe_ends[1], STDOUT_FILENO) < 0) {
               perror("dup2 failed");
               exit(EXIT_FAILURE);
           }
           close(pipe_ends[1]); /* close write end and execute left command */
           if (execvp(args[0], args) == -1) {
               perror("execvp (left command) failed");
           }
           exit(EXIT_FAILURE);
       }


       /*  Fork second child for right-hand command */
       pid_t pid2 = fork();
       if (pid2 < 0) {
           perror("fork failed");
           return 1;
       } else if (pid2 == 0) {
           /* child process for right hand */
           close(pipe_ends[1]);  /*  Close unused write end */
           if (dup2(pipe_ends[0], STDIN_FILENO) < 0) {
               perror("dup2 failed");
               exit(EXIT_FAILURE);
           }
           close(pipe_ends[0]);    /* close read end and execute right command */
           if (execvp(args[pipe_index + 1], &args[pipe_index + 1]) == -1) {
               perror("execvp (right command) failed");
           }
           exit(EXIT_FAILURE);
       }


       /*  Parent process: close both ends of the pipe and wait for both children */
       close(pipe_ends[0]);
       close(pipe_ends[1]);
       waitpid(pid1, NULL, 0);
       waitpid(pid2, NULL, 0);


       return 1; /* worked */
   }
   return 0;   /* didnt work */
}


/*
* Function: handle_input_or_output
* --------------------------------
* Identifies and processes input (<) and output (>) redirection in the command.
*/
void handle_input_or_output(char *args[], int argc, char **input_file, char **output_file) {
   for (int j = 0; j < argc; j++) {
       /* output redirection check */
       if (strcmp(args[j], ">") == 0) {
           *output_file = args[j + 1]; /* store filename */
           args[j] = NULL; /* cut off command at operator */
           break;
       } else if (strcmp(args[j], "<") == 0) {
           *input_file = args[j + 1];  /* store filename */
           args[j] = NULL; /* cut off command at operator */
           break;
       }
   }
}


/*
* Function: run_instruction
* -------------------------
* Executes the command in a child process, handling background execution and I/O redirection.
*/
void run_instruction(char *args[], int background, char *input_file, char *output_file) {
   pid_t pid = fork();
   if (pid < 0) {
       perror("fork failed");
       return;
   }
   else if (pid == 0) {  /* Child process */


       if (output_file != NULL) {
           /* open file for writing, create one if it doesnt exist */
           int redirect_fd  = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
           if (redirect_fd  < 0) { /* error check */
               fprintf(stderr, "Error: Unable to open output file '%s'\n", output_file);
               exit(EXIT_FAILURE);
           }
           if (dup2(redirect_fd , STDOUT_FILENO) < 0) {    /* redirect stdout to the file */
               fprintf(stderr, "Error: Unable to redirect standard output to '%s'\n", output_file);
               exit(EXIT_FAILURE);
           }
           /* close file descriptor */
           close(redirect_fd );
       /* input redirection */
       } else if (input_file != NULL) {
           /* open file for reading ONLY */
           int redirect_fd  = open(input_file, O_RDONLY);
           if (redirect_fd  < 0) { /* error check */
               fprintf(stderr, "Error: Unable to open input file '%s'\n", input_file);
               exit(EXIT_FAILURE);
           }
           if (dup2(redirect_fd , STDIN_FILENO) < 0) {/* redorect stdin to the file */
               fprintf(stderr, "Error: Unable to redirect standard input from '%s'\n", input_file);
               exit(EXIT_FAILURE);
           }
           /* close file descriptor */
           close(redirect_fd );
       }


       /* Execute the actual command */
       if (execvp(args[0], args) == -1) {
           perror("execvp failed");
       }
       _exit(EXIT_FAILURE);
   }
   else { /* Parent process */
       if (!background) {
           waitpid(pid, NULL, 0);
       } else {
           printf("Process running in background (PID: %d)\n", pid);
           fflush(stdout);
       }
   }
}


/*
* Main function:
* --------------
* The main loop of the shell, handling prompt printing, user input, argument division, etc.
*/
int main(void) {
   enable_noncanonical_mode();


   char input[MAX_LENGTH];  /* stores user input */
   char *args[MAX_ARGS];    /* stores arguments */
   int background = 0;      /* background flag */
   int argc = 0;            /* number of arguments */


   while (1) {
       /* Print the prompt once per loop, right before reading input. */
       print_prompt();


       /*  Get user input using non-canonical mode (with arrow key handling) */
       if (get_input(input) < 0)
           break;


       /*  Ignore empty input */
       if (strlen(input) == 0)
           continue;


       /* Check for !! */
       if (strcmp(input, "!!") == 0) {
           if (command_count == 0) {
               printf("No commands in history.\n");
               continue;
           }
           int last_index = (next_command - 1 + BUFFER_SIZE) % BUFFER_SIZE;
           char last_cmd[MAX_LENGTH];
           strcpy(last_cmd, history[last_index]);
           printf("%s\n", last_cmd); /* display last command */
           strcpy(input, last_cmd);  /* Replace input with the last command */
       } else {
           /*  Add non-"!!" commands to history */
           add_to_buffer(input);
       }


       background = 0; /* Reset background flag for each new command */


       /* Split input into arguments, check for & */
       argc = divide_args(input, args, &background);
       if (args[0] == NULL)
           continue;


       /* Handle cd/exit built-ins */
       if (handle_cd_and_exit(args))
           continue;


       /* Check for pipe */
       if (handle_pipe(args, argc))
           continue;


       /* Check for < or > redirection */
       char *input_file = NULL;
       char *output_file = NULL;
       handle_input_or_output(args, argc, &input_file, &output_file);


       /* Execute the command */
       run_instruction(args, background, input_file, output_file);
   }
   return 0;
}
