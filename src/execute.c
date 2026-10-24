/**
 * @file execute.c
 *
 * @brief Implements interface functions between Quash and the environment and
 * functions that interpret an execute commands.
 *
 * @note As you add things to this file you may want to change the method signature
 */

#include "execute.h"

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include "quash.h"

// Remove this and all expansion calls to it
/**
 * @brief Note calls to any function that requires implementation
 */
#define IMPLEMENT_ME()                                                  \
  fprintf(stderr, "IMPLEMENT ME: %s(line %d): %s()\n", __FILE__, __LINE__, __FUNCTION__)

typedef struct Job {
  int id;
  pid_t* pids;
  bool* finished;
  size_t process_count;
  pid_t first_pid;
  char* command;
  struct Job* next;
} Job;

static Job* job_list = NULL;
static int next_job_id = 1;
static int process_input_fd = -1;
static int process_output_fd = -1;
/***************************************************************************
 * Interface Functions
 ***************************************************************************/

// Return a string containing the current working directory.
char* get_current_directory(bool* should_free) {
  // TODO: Get the current working directory. This will fix the prompt path.
  // HINT: This should be pretty simple

  char* directory = getcwd(NULL, 0);

  if (directory == NULL) {
    perror("ERROR: Failed to get current directory");
    *should_free = false;
    return NULL;
  }

  // Change this to true if necessary
  *should_free = true;

  return directory;
}

// Returns the value of an environment variable env_var
const char* lookup_env(const char* env_var) {
  // TODO: Lookup environment variables. This is required for parser to be able
  // to interpret variables from the command line and display the prompt
  // correctly
  // HINT: This should be pretty simple

  return getenv(env_var);
}

// Implemented this as it is declared in the function
void write_env(const char* env_var, const char* val) {
  if (env_var == NULL || val == NULL) {
    fprintf(stderr, "ERROR: Invalid environment variable\n");
    return;
  }

  if (setenv(env_var, val, 1) == -1) {
    perror("ERROR: Failed to set environment variable");
  }
}

// Check the status of background jobs
void check_jobs_bg_status() {
  Job** current = &job_list;

  while (*current != NULL) {
    Job* job = *current;
    bool all_finished = true;

    for (size_t i = 0; i < job->process_count; ++i) {
      if (job->finished[i]) {
        continue;
      }

      int status;
      pid_t result = waitpid(job->pids[i], &status, WNOHANG);
      if (result == job->pids[i]) {
        job->finished[i] = true;
      }
      else if (result == -1 && errno == ECHILD) {
        job->finished[i] = true;
      }
      else if (result == -1) {
        perror("ERROR: Failed to check background job");
      }

      if (!job->finished[i]) {
        all_finished = false;
      }
    }

    if (!all_finished) {
      current = &job->next;
      continue;
    }

    print_job_bg_complete(job->id, job->first_pid, job->command);
    *current = job->next;
    free(job->pids);
    free(job->finished);
    free(job->command);
    free(job);
  }
}

// Prints the job id number, the process id of the first process belonging to
// the Job, and the command string associated with this job
void print_job(int job_id, pid_t pid, const char* cmd) {
  printf("[%d]\t%8d\t%s\n", job_id, pid, cmd);
  fflush(stdout);
}

// Prints a start up message for background processes
void print_job_bg_start(int job_id, pid_t pid, const char* cmd) {
  printf("Background job started: ");
  print_job(job_id, pid, cmd);
}

// Prints a completion message followed by the print job
void print_job_bg_complete(int job_id, pid_t pid, const char* cmd) {
  printf("Completed: \t");
  print_job(job_id, pid, cmd);
}

/***************************************************************************
 * Functions to process commands
 ***************************************************************************/
// Run a program reachable by the path environment variable, relative path, or
// absolute path
void run_generic(GenericCommand cmd) {
  // Execute a program with a list of arguments. The `args` array is a NULL
  // terminated (last string is always NULL) list of strings. The first element
  // in the array is the executable
  char* exec = cmd.args[0];
  char** args = cmd.args;

  // TODO: Remove warning silencers
  (void) exec; // Silence unused variable warning
  (void) args; // Silence unused variable warning

  // TODO: Implement run generic
  if (exec == NULL) {
    return;
  }

  execvp(exec, args);
  perror("ERROR: Failed to execute program");
}

// Print strings
void run_echo(EchoCommand cmd) {
  // Print an array of strings. The args array is a NULL terminated (last
  // string is always NULL) list of strings.
  char** str = cmd.args;

  // TODO: Remove warning silencers
  (void) str; // Silence unused variable warning

  // TODO: Implement echo
  for (int i = 0; str != NULL && str[i] != NULL; ++i) {
    if (i > 0) {
      putchar(' ');
    }
    fputs(str[i], stdout);
  }

  putchar('\n');

  // Flush the buffer before returning
  fflush(stdout);
}

// Sets an environment variable
void run_export(ExportCommand cmd) {
  // Write an environment variable
  const char* env_var = cmd.env_var;
  const char* val = cmd.val;

  // TODO: Remove warning silencers
  (void) env_var; // Silence unused variable warning
  (void) val;     // Silence unused variable warning

  // TODO: Implement export.
  // HINT: This should be quite simple.
  if (env_var == NULL || val == NULL) {
    fprintf(stderr, "ERROR: export requires a variable name and value\n");
    return;
  }

  write_env(env_var, val);
}

// Changes the current working directory
void run_cd(CDCommand cmd) {
  // Get the directory name
  const char* dir = cmd.dir;

  // Check if the directory is valid
  if (dir == NULL) {
    perror("ERROR: Failed to resolve path");
    return;
  }

  // TODO: Change directory

  // TODO: Update the PWD environment variable to be the new current working
  // directory and optionally update OLD_PWD environment variable to be the old
  // working directory.
  bool should_free = false;
  char* old_dir = get_current_directory(&should_free);

  if (chdir(dir) == -1) {
    perror("ERROR: Failed to change directory");
    if (should_free) {
      free(old_dir);
    }
    return;
  }

  if (old_dir != NULL) {
    write_env("OLD_PWD", old_dir);
    if (should_free) {
      free(old_dir);
    }
  }

  char* new_dir = get_current_directory(&should_free);
  if (new_dir != NULL) {
    write_env("PWD", new_dir);
    if (should_free) {
      free(new_dir);
    }
  }
}

// Sends a signal to all processes contained in a job
void run_kill(KillCommand cmd) {
  int signal = cmd.sig;
  int job_id = cmd.job;

  if (signal <= 0 || signal >= NSIG) {
    fprintf(stderr, "ERROR: Invalid signal %d\n", signal);
    return;
  }

  Job* job = job_list;
  while (job != NULL && job->id != job_id) {
    job = job->next;
  }

  if (job == NULL) {
    fprintf(stderr, "ERROR: No background job with id %d\n", job_id);
    return;
  }

  for (size_t i = 0; i < job->process_count; ++i) {
    if (job->finished[i]) {
      continue;
    }

    if (kill(job->pids[i], signal) == -1) {
      if (errno == ESRCH) {
        fprintf(stderr, "ERROR: Process %d no longer exists\n",
                job->pids[i]);
      }
      else {
        perror("ERROR: Failed to send signal");
      }
    }
  }
}


// Prints the current working directory to stdout
void run_pwd() {
  // TODO: Print the current working directory
  bool should_free = false;
  char* directory = get_current_directory(&should_free);

  if (directory == NULL) {
    return;
  }

  puts(directory);

  if (should_free) {
    free(directory);
  }

  // Flush the buffer before returning
  fflush(stdout);
}

// Prints all background jobs currently in the job list to stdout
void run_jobs() {
  for (Job* job = job_list; job != NULL; job = job->next) {
    print_job(job->id, job->first_pid, job->command);
  }

  // Flush the buffer before returning
  fflush(stdout);
}

/***************************************************************************
 * Functions for command resolution and process setup
 ***************************************************************************/

/**
 * @brief A dispatch function to resolve the correct @a Command variant
 * function for child processes.
 *
 * This version of the function is tailored to commands that should be run in
 * the child process of a fork.
 *
 * @param cmd The Command to try to run
 *
 * @sa Command
 */
void child_run_command(Command cmd) {
  CommandType type = get_command_type(cmd);

  switch (type) {
  case GENERIC:
    run_generic(cmd.generic);
    break;

  case ECHO:
    run_echo(cmd.echo);
    break;

  case PWD:
    run_pwd();
    break;

  case JOBS:
    run_jobs();
    break;

  case EXPORT:
  case CD:
  case KILL:
  case EXIT:
  case EOC:
    break;

  default:
    fprintf(stderr, "Unknown command type: %d\n", type);
  }
}

/**
 * @brief A dispatch function to resolve the correct @a Command variant
 * function for the quash process.
 *
 * This version of the function is tailored to commands that should be run in
 * the parent process (quash).
 *
 * @param cmd The Command to try to run
 *
 * @sa Command
 */
void parent_run_command(Command cmd) {
  CommandType type = get_command_type(cmd);

  switch (type) {
  case EXPORT:
    run_export(cmd.export);
    break;

  case CD:
    run_cd(cmd.cd);
    break;

  case KILL:
    run_kill(cmd.kill);
    break;

  case GENERIC:
  case ECHO:
  case PWD:
  case JOBS:
  case EXIT:
  case EOC:
    break;

  default:
    fprintf(stderr, "Unknown command type: %d\n", type);
  }
}

/**
 * @brief Creates one new process centered around the @a Command in the @a
 * CommandHolder setting up redirects and pipes where needed
 *
 * @note Processes are not the same as jobs. A single job can have multiple
 * processes running under it. This function creates a process that is part of a
 * larger job.
 *
 * @note Not all commands should be run in the child process. A few need to
 * change the quash process in some way
 *
 * @param holder The CommandHolder to try to run
 *
 * @sa Command CommandHolder
 */
pid_t create_process(CommandHolder holder) {
  // Read the flags field from the parser
  bool p_in  = holder.flags & PIPE_IN;
  bool p_out = holder.flags & PIPE_OUT;
  bool r_in  = holder.flags & REDIRECT_IN;
  bool r_out = holder.flags & REDIRECT_OUT;
  bool r_app = holder.flags & REDIRECT_APPEND; // This can only be true if r_out
                                               // is true

  CommandType type = get_command_holder_type(holder);
  if (!(holder.flags & BACKGROUND) && !p_in && !p_out &&
      (type == CD || type == EXPORT || type == KILL)) {
    parent_run_command(holder.cmd);
    return 0;
  }

  // TODO: Setup pipes, redirects, and new process
  pid_t pid = fork();

  if (pid < 0) {
    perror("ERROR: fork failed");
    return -1;
  }

  if (pid == 0) {
    if (process_input_fd != -1 &&
        dup2(process_input_fd, STDIN_FILENO) == -1) {
      perror("ERROR: Failed to connect pipe input");
      exit(EXIT_FAILURE);
    }
    if (process_output_fd != -1 &&
        dup2(process_output_fd, STDOUT_FILENO) == -1) {
      perror("ERROR: Failed to connect pipe output");
      exit(EXIT_FAILURE);
    }

    if (r_in) {
      int fd = open(holder.redirect_in, O_RDONLY);
      if (fd == -1) {
        perror("ERROR: Failed to open input file");
        exit(EXIT_FAILURE);
      }
      if (dup2(fd, STDIN_FILENO) == -1) {
        perror("ERROR: Failed to redirect input");
        close(fd);
        exit(EXIT_FAILURE);
      }
      close(fd);
    }

    if (r_out) {
      int flags = O_WRONLY | O_CREAT | (r_app ? O_APPEND : O_TRUNC);
      int fd = open(holder.redirect_out, flags, 0666);
      if (fd == -1) {
        perror("ERROR: Failed to open output file");
        exit(EXIT_FAILURE);
      }
      if (dup2(fd, STDOUT_FILENO) == -1) {
        perror("ERROR: Failed to redirect output");
        close(fd);
        exit(EXIT_FAILURE);
      }
      close(fd);
    }

    child_run_command(holder.cmd);
    exit(EXIT_FAILURE);
  }

  if (process_input_fd != -1) {
    close(process_input_fd);
    process_input_fd = -1;
  }
  if (process_output_fd != -1) {
    close(process_output_fd);
    process_output_fd = -1;
  }

  if (!(holder.flags & BACKGROUND) && !p_in && !p_out) {
    int status;

    if (waitpid(pid, &status, 0) == -1) {
      perror("ERROR: waitpid failed");
    }
  }

  return pid;
}

// Run a list of commands
void run_script(CommandHolder* holders) {
  if (holders == NULL)
    return;

  check_jobs_bg_status();

  if (get_command_holder_type(holders[0]) == EXIT &&
      get_command_holder_type(holders[1]) == EOC) {
    end_main_loop();
    return;
  }

  size_t holder_count = 0;
  while (get_command_holder_type(holders[holder_count]) != EOC) {
    ++holder_count;
  }

  pid_t* pids = malloc(holder_count * sizeof(pid_t));
  if (pids == NULL) {
    perror("ERROR: Failed to allocate process list");
    return;
  }

  CommandType type;
  size_t process_count = 0;
  int previous_pipe_read = -1;

  for (int i = 0; (type = get_command_holder_type(holders[i])) != EOC; ++i) {
    int pipe_fds[2] = {-1, -1};
    bool pipe_out = holders[i].flags & PIPE_OUT;

    if (pipe_out && pipe(pipe_fds) == -1) {
      perror("ERROR: Failed to create pipe");
      free(pids);
      return;
    }

    process_input_fd = previous_pipe_read;
    process_output_fd = pipe_out ? pipe_fds[1] : -1;

    pid_t pid = create_process(holders[i]);

    if (pid == -1) {
      if (pipe_fds[0] != -1) close(pipe_fds[0]);
      if (pipe_fds[1] != -1) close(pipe_fds[1]);
      free(pids);
      return;
    }

    pids[process_count++] = pid;

    previous_pipe_read = pipe_out ? pipe_fds[0] : -1;

  }

  if (!(holders[0].flags & BACKGROUND)) {
    for (size_t i = 0; i < process_count; ++i) {
      if (waitpid(pids[i], NULL, 0) == -1 && errno != ECHILD) {
        perror("ERROR: Failed to wait for process");
      }
    }
    free(pids);
    return;
  }

  Job* job = malloc(sizeof(Job));
  if (job == NULL) {
    perror("ERROR: Failed to allocate background job");
    return;
  }

  job->pids = malloc(process_count * sizeof(pid_t));
  job->finished = malloc(process_count * sizeof(bool));
  job->command = get_command_string();

  if (job->pids == NULL || job->finished == NULL || job->command == NULL) {
    perror("ERROR: Failed to allocate background job data");
    free(pids);
    free(job->finished);
    free(job->command);
    free(job);
    return;
  }

  job->id = next_job_id++;
  job->process_count = process_count;
  job->first_pid = pids[0];
  for (size_t i = 0; i < process_count; ++i) {
    job->pids[i] = pids[i];
    job->finished[i] = false;
  }
  free(pids);
  job->next = job_list;
  job_list = job;

  print_job_bg_start(job->id, job->first_pid, job->command);
}
