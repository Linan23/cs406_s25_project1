// some needed h-files
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/wait.h>

// Path Directories
#define MAX_ARGS 100
#define MAX_PATH_DIRS 100

// global array to store directories for the 'path' command
char *path_dirs[MAX_PATH_DIRS] = {NULL};

/************************************ string formatting commands *************/
int contains_only_ws(char *buf);
void remove_special_characters(char *buf);
void trim_trailing_ws(char *buf);
void trim_leading_ws(char *buf);
void remove_duplicate_ws(char *buf);

// Helper funciton: Inserts spaces around the redirection operator ('>') to ensure proper tokenization
char *insert_spaces_around_redirect(const char *str);

/************************************ path and exec processing   *************/
char **split_args_str(char *str);

/*****************************************************************************/
/************************************ the main event   ***********************/
/*****************************************************************************/

int main(int argc, char *argv[])
{
  char *line = NULL;  // buffer for input line
  size_t bufsize = 0; // buffer size
  FILE *input_stream = stdin;
  int interactive = 1; // flag for interactive mode
  int batch_error = 0; // flag for error in batch mode

  // batch mode is on if only one argument is provided
  if (argc == 2)
  {
    interactive = 0;
    input_stream = fopen(argv[1], "r");
    if (input_stream == NULL)
    {
      char error_message[] = "An error has occurred\n";
      write(STDERR_FILENO, error_message, strlen(error_message));
      exit(1);
    }
  }
  else if (argc > 2)
  {
    char error_message[] = "An error has occurred\n";
    write(STDERR_FILENO, error_message, strlen(error_message));
    exit(1);
  }

  // initialize the default path to /bin
  if (path_dirs[0] == NULL)
  {
    path_dirs[0] = strdup("/bin");
    if (path_dirs[0] == NULL)
    {
      char error_message[] = "An error has occurred\n";
      write(STDERR_FILENO, error_message, strlen(error_message));
      exit(1);
    }
  }

  // loop for the main function:read input, parse, executing commands
  while (1)
  {

    // Print the prompt
    if (interactive)
    {
      printf("lsh> ");
      fflush(stdout);
    }
    // Read line from input stream
    if (getline(&line, &bufsize, input_stream) == -1)
    {
      free(line);
      exit(0);
    }

    // Remove trailing newline if present in input stream
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n')
    {
      line[len - 1] = '\0';
    }

    // Helper functions to clean input
    remove_special_characters(line);
    trim_leading_ws(line);
    trim_trailing_ws(line);
    remove_duplicate_ws(line);

    // function to insert space around redirection operations
    char *processed_line = insert_spaces_around_redirect(line);
    if (processed_line == NULL)
    {
      write(STDERR_FILENO, "An error has occurred\n", 22);
      exit(1);
    }
    free(line);
    line = processed_line;

    // Skip if the line is empty or contains white spaces
    if (contains_only_ws(line))
    {
      continue;
    }

    // Split line through '&' to support parallel commands
    char *command;
    char *rest = line;
    pid_t pids[MAX_ARGS]; // stores child process
    int pid_count = 0;

    // split the input line on '&'
    while ((command = strtok_r(rest, "&", &rest)) != NULL)
    {
      // trim whitespace from individual commands
      trim_leading_ws(command);
      trim_trailing_ws(command);
      if (contains_only_ws(command))
      {
        continue;
      }

      // Split command into tokens using split_args_str()
      char **args = split_args_str(command);
      if (args[1] == NULL)
      {
        free(args);
        continue;
      }
      // command will start at idex 1
      char **cmd_args = args + 1;

      // built-in commands
      if (strcmp(cmd_args[0], "exit") == 0)
      {
        //in batch mode, any extra arguments are considered errors
        if (cmd_args[1] != NULL)
        {
          char error_message[] = "An error has occurred\n";
          write(STDERR_FILENO, error_message, strlen(error_message));
          free(args);
          continue;
        }
        //no extra arguments, exit normally.
        free(args);
        free(line);
        fclose(input_stream);
        exit(0);
      }

      // cd command
      if (strcmp(cmd_args[0], "cd") == 0)
      {
        if (cmd_args[1] == NULL || cmd_args[2] != NULL)
        {
          char error_message[] = "An error has occurred\n";
          write(STDERR_FILENO, error_message, strlen(error_message));
        }
        else
        {
          if (chdir(cmd_args[1]) != 0)
          {
            perror("cd");
          }
        }
        free(args);
        continue;
      }

      // path
      if (strcmp(cmd_args[0], "path") == 0)
      {

        // free any previously allocated path string
        for (int j = 0; j < MAX_PATH_DIRS; j++)
        {
          if (path_dirs[j] != NULL)
          {
            free(path_dirs[j]);
            path_dirs[j] = NULL;
          }
        }
        // new directories are copied into the global path_dir array
        for (int j = 1; cmd_args[j] != NULL && (j - 1) < MAX_PATH_DIRS; j++)
        {
          path_dirs[j - 1] = strdup(cmd_args[j]);
          if (path_dirs[j - 1] == NULL)
          {
            write(STDERR_FILENO, "An error has occurred\n", 22);
            exit(1);
          }
        }
        free(args);
        continue;
      }

      // output redirection

      // checks > that no commands are provided
      if (strcmp(cmd_args[0], ">") == 0)
      {
        char error_message[] = "An error has occurred\n";
        write(STDERR_FILENO, error_message, strlen(error_message));
        if (!interactive)
          batch_error = 1;
        free(args);
        continue;
      }
      int fd_out = -1;

      // iterate through the tokens to detect redirection operator
      for (int i = 0; cmd_args[i] != NULL; i++)
      {
        if (strcmp(cmd_args[i], ">") == 0)
        {
          // check that there is exactly one token following >
          if (cmd_args[i + 1] == NULL || cmd_args[i + 2] != NULL)
          {
            char error_message[] = "An error has occurred\n";
            write(STDERR_FILENO, error_message, strlen(error_message));
            if (!interactive)
              batch_error = 1;
            goto next_command;
          }
          fd_out = open(cmd_args[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
          if (fd_out < 0)
          {
            perror("open");
            if (!interactive)
              batch_error = 1;
            goto next_command;
          }
          // remove redirection operator and filename from the arguments
          cmd_args[i] = NULL;
          break;
        }
      }

      // FORKING

      pid_t pid = fork();
      if (pid < 0)
      {
        char error_message[] = "An error has occurred\n";
        write(STDERR_FILENO, error_message, strlen(error_message));
      }
      else if (pid == 0)
      { // Child process
        if (fd_out != -1)
        {
          dup2(fd_out, STDOUT_FILENO);
          dup2(fd_out, STDERR_FILENO);
          close(fd_out);
        }

        // iterate through each directory in path_dirs to find and execute the command
        for (int d = 0; d < MAX_PATH_DIRS && path_dirs[d] != NULL; d++)
        {
          char prog_path[512];
          snprintf(prog_path, sizeof(prog_path), "%s/%s", path_dirs[d], cmd_args[0]);
          if (access(prog_path, X_OK) == 0)
          {
            execv(prog_path, cmd_args);
          }
        }
        // error handling if no valid executable is found
        write(STDERR_FILENO, "An error has occurred\n", 22);
        exit(1);
      }
      else
      { // Parent process
        pids[pid_count++] = pid;
        if (fd_out != -1)
        {
          close(fd_out);
        }
      }
    next_command:
      free(args);
    }

    // wait for all parallel child processes to finish before starting again
    for (int i = 0; i < pid_count; i++)
    {
      waitpid(pids[i], NULL, 0);
    }
  }

  // clean input stream if in batch mode
  if (!interactive)
  {
    fclose(input_stream);
  }
  free(line);
  if (!interactive && batch_error)
    exit(1);
  else
    exit(0);
}

/*****************************************************************************/
/************************************ some helpful code  *********************/
/*****************************************************************************/

//-- Checks a command string to see if it is only whitespace, returning 1
//-- if it is true.
int contains_only_ws(char *buf)
{

  while (*buf)
  {
    switch (*buf)
    {
    case '\n':
    case ' ':
    case '\t':
    {
      buf++;
      break;
    }
    default:
    {
      return 0;
    }
    }
  }

  return 1;
}

//-- Change all special characters like newline and tab to characters.
void remove_special_characters(char *buf)
{

  while (*buf)
  {

    switch (*buf)
    {
    case '\n':
    case '\t':
    case '\r':
    {
      *buf = ' ';
      break;
    }
    default:
    {
      // do nothing
    }
    }

    buf++;
  }

  return;
}

//-- Trims the trailing spaces by moving the null terminator backwards.
void trim_trailing_ws(char *buf)
{

  int spaces_found = 0;

  char *ptr = buf; // initialize

  while (*ptr)
    ptr++; // find null terminator

  ptr--; // backup one position

  while (*ptr == ' ')
  { // backup null pointer over the spaces
    ptr--;
    spaces_found = 1;
  }

  if (spaces_found)
  {                    // place null terminator at the
    *(ptr + 1) = '\0'; // first space
  }

  return;
}

//-- Remove spaces from the front of the line by shifting the string
//-- starting at the first non-space character.
void trim_leading_ws(char *buf)
{

  char *ptr_from = buf; // initialize source and destiation pointers
  char *ptr_to = buf;

  if (*buf == ' ')
  { // leading spaces found ---------------------------------

    while (*ptr_from == ' ')
    { // find the start of the string
      ptr_from++;
    }

    while (*ptr_from != '\0')
    { // copy everything but null terminator
      *ptr_to = *ptr_from;
      ptr_to++;
      ptr_from++;
    }

    *ptr_to = *ptr_from; // copy null terminator
    ptr_to++;

    while (*ptr_from != '\0')
    { // clear trailing items, probably not needed
      *ptr_to = ' ';
      ptr_to++;
    }
    *ptr_to = ' ';
  }

  return;
}

//-- Removes all extra-spaces by looking for repeated spaces and skippping
//-- over them.  This method modifies the passed string, but the string
//-- will never grow only shrink but removing excess characters.
void remove_duplicate_ws(char *buf)
{

  char *ptr_from = buf; // initialize source and destiation pointers
  char *ptr_to = buf;

  int copying = 0; // Flag is set when multiple spaces are detected
                   // and copying from one part of the string to another.

  while (*ptr_from != '\0')
  {

    if (copying)
    { /*** dup spaces detected, copying        ***/

      switch (*ptr_from)
      {
      case ' ':
      {

        *ptr_to = *ptr_from; // copy first space then increment ptrs
        ptr_to++;
        ptr_from++;

        while (*ptr_from == ' ')
        {             // find next non-space incrementing the
          ptr_from++; // from pointer
        }
        break;
      }
      default:
      {
        *ptr_to = *ptr_from; // all other characters are copied
        ptr_to++;
        ptr_from++;
        break;
      }
      }
    }
    else
    { /*** dup spaces not yet detected         ***/

      if (*ptr_from == ' ' &&
          *(ptr_from + 1) == ' ')
      { // dup spaces have been detected

        copying = 1; // flag set switching processing mode

        ptr_to = ptr_from + 1; // initialize destination pointer

        while (*ptr_from == ' ')
        { // move source ptr to next non-space
          ptr_from++;
        }

        *ptr_to = *ptr_from; // start to copy process
        ptr_to++;
        ptr_from++;
      }
      else
      { /*** no dup spaces, just iterate         ***/
        ptr_from++;
      }
    }
  }

  if (copying)
  {
    *ptr_to = *ptr_from; // copy the null-terminator character, if copying
  }

  return;
}
//-- Accepts a string with command arguments, and splits them into
//-- parts.  A character pointer array is created with an empty slot
//-- at index 0 for the command.  The char pointer array is returned
//-- as a double pointer char.
char **split_args_str(char *str)
{

  int cnt_spaces = 1; // initialize for a single argument,
                      // there will be no associated space

  char *ptr = str; // initialize pointer

  while (*ptr)
  { // count spaces for additional arguments
    if (*ptr == ' ')
      cnt_spaces++;
    ptr++;
  }

  cnt_spaces += 2; // add one slot for both the command and
                   // terminating null

  // create array for each argument, the command a index 0, and a
  // terminating null pointer.
  char **ptr_array = malloc((cnt_spaces + 2) * sizeof(char **));

  ptr = str; // reset the point to front of str

  ptr_array[0] = NULL; // initialize command slot as null and
  int index = 1;       // start index at next slot

  ptr_array[index] = ptr; // store the first argument
  index++;                // index the next slot

  while (*ptr)
  {
    if (*ptr == ' ')
    {
      *ptr = '\0';                // terminate current argument
      ptr_array[index] = ptr + 1; // store next argument
      index++;                    // index the next slot
    }
    ptr++;
  }

  ptr_array[index] = NULL; // store the terminating null

  return ptr_array;
}

// Helper function: inserts spaces around > in input string

char *insert_spaces_around_redirect(const char *str)
{
  int len = strlen(str);
  // ensure space is enough if all character is >
  int new_len = len * 3 + 1;
  char *new_str = malloc(new_len);
  if (new_str == NULL)
  {
    return NULL;
  }
  int i = 0, j = 0;
  while (str[i] != '\0')
  {
    if (str[i] == '>')
    {
      // Insert a space before '>' if not at beginning and previous isn't a space.
      if (j > 0 && new_str[j - 1] != ' ')
        new_str[j++] = ' ';
      new_str[j++] = '>';
      // Insert a space after '>' if next character is not a space and not end-of-string.
      if (str[i + 1] != ' ' && str[i + 1] != '\0')
        new_str[j++] = ' ';
      i++;
    }
    else
    {
      new_str[j++] = str[i++];
    }
  }
  new_str[j] = '\0';
  return new_str;
}
