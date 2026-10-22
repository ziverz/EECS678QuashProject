/**
 * @file execute.c
 *
 * @brief Implements command execution for Quash.
 */

#define _GNU_SOURCE
#include "execute.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "quash.h"

typedef struct Job {
  int id;
  pid_t *pids;
  bool *finished;
  size_t count;
  char *command;
  struct Job *next;
} Job;

static Job *jobs;

static CommandType command_type(CommandHolder h) {
  return get_command_holder_type(h);
}

static void free_job(Job *job) {
  if (job == NULL) return;
  free(job->pids);
  free(job->finished);
  free(job->command);
  free(job);
}

static void remove_job(Job **link) {
  Job *job = *link;
  *link = job->next;
  free_job(job);
}

char* get_current_directory(bool* should_free) {
  char *cwd = get_current_dir_name();
  *should_free = cwd != NULL;
  return cwd;
}

const char* lookup_env(const char* env_var) {
  const char *value = getenv(env_var);
  static char hostname[256];
  if (value == NULL && env_var != NULL && strcmp(env_var, "HOSTNAME") == 0) {
    if (gethostname(hostname, sizeof(hostname) - 1) == 0)
      return hostname;
  }
  return value;
}

void write_env(const char* env_var, const char* val) {
  if (env_var != NULL && val != NULL && setenv(env_var, val, 1) < 0)
    perror("export");
}

void check_jobs_bg_status() {
  Job **link = &jobs;
  while (*link != NULL) {
    Job *job = *link;
    bool done = true;
    for (size_t i = 0; i < job->count; ++i) {
      if (job->finished[i]) continue;
      int status;
      pid_t result = waitpid(job->pids[i], &status, WNOHANG);
      if (result == job->pids[i] || (result < 0 && errno == ECHILD))
        job->finished[i] = true;
      else
        done = false;
    }
    for (size_t i = 0; i < job->count; ++i)
      if (!job->finished[i]) done = false;
    if (done) {
      print_job_bg_complete(job->id, job->pids[0], job->command);
      remove_job(link);
    } else {
      link = &job->next;
    }
  }
}

void print_job(int job_id, pid_t pid, const char* cmd) {
  printf("[%d]\t%8d\t%s\n", job_id, pid, cmd != NULL ? cmd : "");
  fflush(stdout);
}

void print_job_bg_start(int job_id, pid_t pid, const char* cmd) {
  printf("Background job started: ");
  print_job(job_id, pid, cmd);
}

void print_job_bg_complete(int job_id, pid_t pid, const char* cmd) {
  printf("Completed: \t");
  print_job(job_id, pid, cmd);
}

void run_generic(GenericCommand cmd) {
  if (cmd.args == NULL || cmd.args[0] == NULL) _exit(EXIT_SUCCESS);
  execvp(cmd.args[0], cmd.args);
  perror("ERROR: Failed to execute program");
  _exit(127);
}

void run_echo(EchoCommand cmd) {
  if (cmd.args != NULL) {
    for (size_t i = 0; cmd.args[i] != NULL; ++i) {
      if (i != 0) putchar(' ');
      fputs(cmd.args[i], stdout);
    }
  }
  putchar('\n');
  fflush(stdout);
}

void run_export(ExportCommand cmd) {
  if (cmd.env_var != NULL && cmd.val != NULL &&
      setenv(cmd.env_var, cmd.val, 1) < 0)
    perror("export");
}

void run_cd(CDCommand cmd) {
  const char *dir = cmd.dir;
  if (dir == NULL) {
    dir = getenv("HOME");
    if (dir == NULL) {
      fprintf(stderr, "cd: HOME not set\n");
      return;
    }
  }
  char *old = get_current_dir_name();
  if (chdir(dir) < 0) {
    perror("cd");
    free(old);
    return;
  }
  char *cwd = get_current_dir_name();
  if (cwd != NULL) {
    setenv("PWD", cwd, 1);
    free(cwd);
  }
  if (old != NULL) {
    setenv("OLDPWD", old, 1);
    free(old);
  }
}

void run_kill(KillCommand cmd) {
  for (Job *job = jobs; job != NULL; job = job->next) {
    if (job->id != cmd.job) continue;
    for (size_t i = 0; i < job->count; ++i) {
      if (!job->finished[i] && kill(job->pids[i], cmd.sig) < 0 && errno != ESRCH)
        perror("kill");
      if (!job->finished[i] && cmd.sig == SIGKILL) {
        while (waitpid(job->pids[i], NULL, 0) < 0 && errno == EINTR) {}
        job->finished[i] = true;
      }
    }
    if (cmd.sig == SIGKILL) {
      bool done = true;
      for (size_t i = 0; i < job->count; ++i)
        if (!job->finished[i]) done = false;
      if (done) {
        print_job_bg_complete(job->id, job->pids[0], job->command);
        Job **link = &jobs;
        while (*link != NULL && *link != job) link = &(*link)->next;
        if (*link == job) remove_job(link);
      }
    }
    return;
  }
  fprintf(stderr, "kill: job %d not found\n", cmd.job);
}

void run_pwd() {
  char *cwd = get_current_dir_name();
  if (cwd == NULL) {
    perror("pwd");
    return;
  }
  puts(cwd);
  free(cwd);
  fflush(stdout);
}

void run_jobs() {
  for (Job *job = jobs; job != NULL; job = job->next)
    print_job(job->id, job->pids[0], job->command);
  fflush(stdout);
}

void child_run_command(Command cmd) {
  switch (get_command_type(cmd)) {
  case GENERIC: run_generic(cmd.generic); break;
  case ECHO: run_echo(cmd.echo); break;
  case PWD: run_pwd(); break;
  case JOBS: run_jobs(); break;
  case EXPORT: run_export(cmd.export); break;
  case CD: run_cd(cmd.cd); break;
  case KILL: run_kill(cmd.kill); break;
  case EXIT: _exit(EXIT_SUCCESS);
  case EOC: break;
  default: fprintf(stderr, "Unknown command type: %d\n", get_command_type(cmd)); break;
  }
  _exit(EXIT_SUCCESS);
}

void parent_run_command(Command cmd) {
  switch (get_command_type(cmd)) {
  case EXPORT: run_export(cmd.export); break;
  case CD: run_cd(cmd.cd); break;
  case KILL: run_kill(cmd.kill); break;
  default: break;
  }
}

static int redirect_input(const char *path) {
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    perror(path);
    return -1;
  }
  if (dup2(fd, STDIN_FILENO) < 0) {
    perror("dup2");
    close(fd);
    return -1;
  }
  close(fd);
  return 0;
}

static int redirect_output(const char *path, bool append) {
  int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
  int fd = open(path, flags, 0666);
  if (fd < 0) {
    perror(path);
    return -1;
  }
  if (dup2(fd, STDOUT_FILENO) < 0) {
    perror("dup2");
    close(fd);
    return -1;
  }
  close(fd);
  return 0;
}

void create_process(CommandHolder holder) {
  (void)holder;
}

static int run_parent_redirected(CommandHolder holder) {
  int saved_in = dup(STDIN_FILENO);
  int saved_out = dup(STDOUT_FILENO);
  int result = 0;
  if (saved_in < 0 || saved_out < 0) {
    perror("dup");
    if (saved_in >= 0) close(saved_in);
    if (saved_out >= 0) close(saved_out);
    return -1;
  }
  if ((holder.flags & REDIRECT_IN) && redirect_input(holder.redirect_in) < 0)
    result = -1;
  if (result == 0 && (holder.flags & (REDIRECT_OUT | REDIRECT_APPEND)) &&
      redirect_output(holder.redirect_out, holder.flags & REDIRECT_APPEND) < 0)
    result = -1;
  if (result == 0) parent_run_command(holder.cmd);
  fflush(stdout);
  dup2(saved_in, STDIN_FILENO);
  dup2(saved_out, STDOUT_FILENO);
  close(saved_in);
  close(saved_out);
  return result;
}

void run_script(CommandHolder* holders) {
  if (holders == NULL) return;

  signal(SIGPIPE, SIG_IGN);
  check_jobs_bg_status();

  if (command_type(holders[0]) == EXIT && command_type(holders[1]) == EOC) {
    end_main_loop(EXIT_SUCCESS);
    return;
  }
  if (command_type(holders[0]) == EOC) return;

  if ((command_type(holders[0]) == EXPORT || command_type(holders[0]) == CD ||
       command_type(holders[0]) == KILL) &&
      command_type(holders[1]) == EOC && !(holders[0].flags & BACKGROUND)) {
    run_parent_redirected(holders[0]);
    return;
  }

  size_t count = 0;
  while (command_type(holders[count]) != EOC) ++count;
  pid_t *pids = calloc(count, sizeof(*pids));
  if (pids == NULL) {
    perror("calloc");
    return;
  }

  int previous_read = -1;
  size_t started = 0;
  for (size_t i = 0; i < count; ++i) {
    CommandHolder holder = holders[i];
    int pipefd[2] = {-1, -1};
    if (holder.flags & PIPE_OUT) {
      if (pipe(pipefd) < 0) {
        perror("pipe");
        break;
      }
    }

    pid_t pid = fork();
    if (pid < 0) {
      perror("fork");
      if (pipefd[0] >= 0) close(pipefd[0]);
      if (pipefd[1] >= 0) close(pipefd[1]);
      break;
    }
    if (pid == 0) {
      signal(SIGPIPE, SIG_DFL);
      if (previous_read >= 0 && dup2(previous_read, STDIN_FILENO) < 0) {
        perror("dup2");
        _exit(1);
      }
      if (pipefd[1] >= 0 && dup2(pipefd[1], STDOUT_FILENO) < 0) {
        perror("dup2");
        _exit(1);
      }
      if (previous_read >= 0) close(previous_read);
      if (pipefd[0] >= 0) close(pipefd[0]);
      if (pipefd[1] >= 0) close(pipefd[1]);
      if ((holder.flags & REDIRECT_IN) && redirect_input(holder.redirect_in) < 0)
        _exit(1);
      if ((holder.flags & (REDIRECT_OUT | REDIRECT_APPEND)) &&
          redirect_output(holder.redirect_out, holder.flags & REDIRECT_APPEND) < 0)
        _exit(1);
      child_run_command(holder.cmd);
    }

    pids[started++] = pid;
    if (previous_read >= 0) close(previous_read);
    if (pipefd[1] >= 0) close(pipefd[1]);
    previous_read = pipefd[0];
  }
  if (previous_read >= 0) close(previous_read);

  if (started == 0) {
    free(pids);
    return;
  }
  if (holders[0].flags & BACKGROUND) {
    Job *job = calloc(1, sizeof(*job));
    if (job == NULL) {
      perror("calloc");
      for (size_t i = 0; i < started; ++i) waitpid(pids[i], NULL, 0);
      free(pids);
      return;
    }
    int max_id = 0;
    for (Job *current = jobs; current != NULL; current = current->next)
      if (current->id > max_id) max_id = current->id;
    job->id = max_id + 1;
    job->pids = pids;
    job->finished = calloc(started, sizeof(*job->finished));
    job->count = started;
    job->command = get_command_string();
    if (job->command == NULL) job->command = strdup("");
    Job **tail = &jobs;
    while (*tail != NULL) tail = &(*tail)->next;
    *tail = job;
    print_job_bg_start(job->id, job->pids[0], job->command);
  } else {
    for (size_t i = 0; i < started; ++i)
      while (waitpid(pids[i], NULL, 0) < 0 && errno == EINTR) {}
    free(pids);
  }
}
