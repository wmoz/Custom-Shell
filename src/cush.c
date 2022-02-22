/*
 * cush - the customizable shell.
 *
 * Developed by Godmar Back for CS 3214 Summer 2020
 * Virginia Tech.  Augmented to use posix_spawn in Fall 2021.
 */
#define _GNU_SOURCE 1
#include <stdio.h>
#include <readline/readline.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <sys/wait.h>
#include <assert.h>
#include <spawn.h>
#include <pwd.h>
#include <fcntl.h>

#include <readline/history.h>

/* Since the handed out code contains a number of unused functions. */
#pragma GCC diagnostic ignored "-Wunused-function"

#include "termstate_management.h"
#include "signal_support.h"
#include "shell-ast.h"
#include "utils.h"
//#include "job_handler.h"

#define IO_IN 0
#define IO_OUT 1
#define IO_APP 2
extern char **environ;

int handle_job(struct ast_pipeline *pipe);
int _posix_spawn_run(pid_t *pid, pid_t pgid, char **argv, bool leader, bool fg,   posix_spawn_file_actions_t* fa);
static void handle_child_status(pid_t pid, int status);
struct job *_get_job_from_pid(pid_t pid);
int handle_builtin(struct ast_pipeline *pipe);
void delete_dead_jobs(void);
int _set_up_io( posix_spawn_file_actions_t* fa, char* iored, int append, int dup_stdout_to_stdin, int __ioflag);
int _close_fds(int* fds, size_t length);
int _set_up_pipe(posix_spawn_file_actions_t* fa, int* newfds, int iteration, int cmd_len);
int* _create_pipes(int num);


static void
usage(char *progname)
{
    printf("Usage: %s -h\n"
           " -h            print this help\n",
           progname);

    exit(EXIT_SUCCESS);
}

/* Build a prompt */
static char *
build_prompt(void)
{
    //getting the hostname
    char hostname[31];
    gethostname(hostname, 31);

    //getting the current directory
    char tempPath[101];
    getcwd(tempPath, 101);
    char *path = basename(tempPath);

    //get username
    struct passwd *pw = getpwuid(getuid());

    //write into buffer
    char buffer[200];
    snprintf(buffer, sizeof buffer, "<%s@%s %s>$ ", pw->pw_name, hostname, path);

    return strdup(buffer);
}

enum job_status
{
    FOREGROUND,    /* job is running in foreground.  Only one job can be
                      in the foreground state. */
    BACKGROUND,    /* job is running in background */
    STOPPED,       /* job is stopped via SIGSTOP */
    NEEDSTERMINAL, /* job is stopped because it was a background job
                      and requires exclusive terminal access */
    DONE            /* Job in the background has finished */ 
};

struct job
{
    struct list_elem elem;          /* Link element for jobs list. */
    struct ast_pipeline *pipe;      /* The pipeline of commands this job represents */
    int jid;                        /* Job id. */
    enum job_status status;         /* Job status. */
    int num_processes_alive;        /* The number of processes that we know to be alive */
    struct termios saved_tty_state; /* The state of the terminal when this job was
                                       stopped after having been in foreground */

    /* Add additional fields here if needed. */
    pid_t pgid; // stores the pgid for the job
};

/* Utility functions for job list management.
 * We use 2 data structures:
 * (a) an array jid2job to quickly find a job based on its id
 * (b) a linked list to support iteration
 */
#define MAXJOBS (1 << 16)
#define MAXCMDS 15 // max num of processes a job can have
static struct list job_list;

static struct job *jid2job[MAXJOBS];

/* Return job corresponding to jid */
static struct job *
get_job_from_jid(int jid)
{
    if (jid > 0 && jid < MAXJOBS && jid2job[jid] != NULL)
        return jid2job[jid];

    return NULL;
}

/* Add a new job to the job list */
static struct job *
add_job(struct ast_pipeline *pipe)
{
    struct job *job = malloc(sizeof *job);
    job->pipe = pipe;
    job->num_processes_alive = 0;
    // Checks if command is being run as foreground or background ~added
    if (pipe->bg_job == true)
        job->status = BACKGROUND;
    else
        job->status = FOREGROUND;

    list_push_back(&job_list, &job->elem);
    for (int i = 1; i < MAXJOBS; i++)
    {
        if (jid2job[i] == NULL)
        {
            jid2job[i] = job;
            job->jid = i;
            return job;
        }
    }

    fprintf(stderr, "Maximum number of jobs exceeded\n");
    abort();
    return NULL;
}

/* Delete a job.
 * This should be called only when all processes that were
 * forked for this job are known to have terminated.
 */
static void
delete_job(struct job *job)
{
    int jid = job->jid;
    assert(jid != -1);
    jid2job[jid]->jid = -1;
    jid2job[jid] = NULL;
    ast_pipeline_free(job->pipe);
    free(job);
}

static const char *
get_status(enum job_status status)
{
    switch (status)
    {
    case FOREGROUND:
        return "Foreground";
    case BACKGROUND:
        return "Running";
    case STOPPED:
        return "Stopped";
    case NEEDSTERMINAL:
        return "Stopped (tty)";
    case DONE:
        return "Done";
    default:
        return "Unknown";
    }
}

/* Print the command line that belongs to one job. */
static void
print_cmdline(struct ast_pipeline *pipeline)
{
    struct list_elem *e = list_begin(&pipeline->commands);
    for (; e != list_end(&pipeline->commands); e = list_next(e))
    {
        struct ast_command *cmd = list_entry(e, struct ast_command, elem);
        if (e != list_begin(&pipeline->commands))
            printf("| ");
        char **p = cmd->argv;
        printf("%s", *p++);
        while (*p)
            printf(" %s", *p++);
    }
}

/* Print a job */
static void
print_job(struct job *job)
{
    // printf("alive: %d\t", job -> num_processes_alive);
    printf("[%d]\t%s\t\t(", job->jid, get_status(job->status));
    print_cmdline(job->pipe);
    printf(")\n");
}

/*
 * Suggested SIGCHLD handler.
 *
 * Call waitpid() to learn about any child processes that
 * have exited or changed status (been stopped, needed the
 * terminal, etc.)
 * Just record the information by updating the job list
 * data structures.  Since the call may be spurious (e.g.
 * an already pending SIGCHLD is delivered even though
 * a foreground process was already reaped), ignore when
 * waitpid returns -1.
 * Use a loop with WNOHANG since only a single SIGCHLD
 * signal may be delivered for multiple children that have
 * exited. All of them need to be reaped.
 */
static void
sigchld_handler(int sig, siginfo_t *info, void *_ctxt)
{
    pid_t child;
    int status;

    assert(sig == SIGCHLD);

    while ((child = waitpid(-1, &status, WUNTRACED | WNOHANG)) > 0)
    {
        handle_child_status(child, status);
    }
}

/* Wait for all processes in this job to complete, or for
 * the job no longer to be in the foreground.
 * You should call this function from a) where you wait for
 * jobs started without the &; and b) where you implement the
 * 'fg' command.
 *
 * Implement handle_child_status such that it records the
 * information obtained from waitpid() for pid 'child.'
 *
 * If a process exited, it must find the job to which it
 * belongs and decrement num_processes_alive.
 *
 * However, note that it is not safe to call delete_job
 * in handle_child_status because wait_for_job assumes that
 * even jobs with no more num_processes_alive haven't been
 * deallocated.  You should postpone deleting completed
 * jobs from the job list until when your code will no
 * longer touch them.
 *
 * The code below relies on `job->status` having been set to FOREGROUND
 * and `job->num_processes_alive` having been set to the number of
 * processes successfully forked for this job.
 */
static void
wait_for_job(struct job *job)
{
    assert(signal_is_blocked(SIGCHLD));

    while (job->status == FOREGROUND && job->num_processes_alive > 0)
    {
        int status;

        pid_t child = waitpid(-1, &status, WUNTRACED);

        // When called here, any error returned by waitpid indicates a logic
        // bug in the shell.
        // In particular, ECHILD "No child process" means that there has
        // already been a successful waitpid() call that reaped the child, so
        // there's likely a bug in handle_child_status where it failed to update
        // the "job" status and/or num_processes_alive fields in the required
        // fashion.
        // Since SIGCHLD is blocked, there cannot be races where a child's exit
        // was handled via the SIGCHLD signal handler.
        if (child != -1)
            handle_child_status(child, status);
        else
            utils_fatal_error("waitpid failed, see code for explanation");
    }
}

/**
 * \brief gets the job with a process associated with the given pid
 *
 * \param pid process  id
 * \return the job that matches the pid
 *
 */
struct job *_get_job_from_pid(pid_t pid)
{
    // truct list_elem *e;
    //  for (e = list_begin(&job_list); e != list_end(&job_list); e = list_next(e))
    //  {
    //      struct job *jobinList = list_entry(e, struct job, elem);

    // }
    for (int i = 0; i < MAXJOBS; i++)
    {
        if (jid2job[i] == NULL)
            continue;
        struct ast_pipeline *pipe = jid2job[i]->pipe;
        // if(pipe == NULL) continue;
        for (struct list_elem *e = list_begin(&pipe->commands);
             e != list_end(&pipe->commands);
             e = list_next(e))
        {
            struct ast_command *cmd = list_entry(e, struct ast_command, elem);
            if (cmd->pid == pid)
                return jid2job[i];
        }
    }
    return NULL;
}

static void
handle_child_status(pid_t pid, int status)
{
    assert(signal_is_blocked(SIGCHLD));

    /* To be implemented.
     * Step 1. Given the pid, determine which job this pid is a part of
     *         (how to do this is not part of the provided code.)
     * Step 2. Determine what status change occurred using the
     *         WIF*() macros.
     * Step 3. Update the job status accordingly, and adjust
     *         num_processes_alive if appropriate.
     *         If a process was stopped, save the terminal state.
     */

    // gets the job through the process group id
    struct job *jobNeeded = _get_job_from_pid(pid);

    /* proccess stop for a temp time:
        User stops fg process with Ctrl-Z
        User stops process with kill -STOP
        non-foreground process wants terminal access
    */
    if (WIFSTOPPED(status))
    {
        // checks if the process is in the foreground
        if (jobNeeded->status == FOREGROUND)
        {
            termstate_save(&jobNeeded->saved_tty_state);
            termstate_give_terminal_back_to_shell();
        }

        if (WSTOPSIG(status) == SIGTTOU || WSTOPSIG(status) == SIGTTIN)
        {
            jobNeeded->status = NEEDSTERMINAL;
        }
        else
        {
            jobNeeded->status = STOPPED;
        }
        print_job(jobNeeded);
    }
    // process exits via exit()
    else if (WIFEXITED(status))
    {
        if (jobNeeded->status == FOREGROUND)
        {
            // termstate_save(&jobNeeded->saved_tty_state);
            if (jobNeeded->num_processes_alive == 1)
            {
                termstate_give_terminal_back_to_shell();
            }
        }
        else if(jobNeeded -> status == BACKGROUND && jobNeeded -> num_processes_alive == 1)
        {
            jobNeeded -> status = DONE;
        }

        jobNeeded->num_processes_alive--;
    }
    // user terminates process
    else if (WIFSIGNALED(status))
    {
        if (jobNeeded->status == FOREGROUND)
        {
            if (jobNeeded->num_processes_alive == 1)
            {
                termstate_give_terminal_back_to_shell();
            }
        }
        jobNeeded->num_processes_alive--;

        // user terminates process with kill
        if (WTERMSIG(status) == SIGTERM)
        {
            printf("Terminated");
        }
        // user terminates process with kill -9
        else if (WTERMSIG(status) == SIGKILL)
        {
            printf("killed");
        }
        // process has been terminated (general case)
        else
        {
            // segmentation fault
            if (WTERMSIG(status) == SIGSEGV)
            {
                printf("segmentation fault");
            }
            // aborted
            else if (WTERMSIG(status) == SIGABRT)
            {
                printf("Aborted");
            }
            // floating point exception
            else if (WTERMSIG(status) == SIGFPE)
            {
                printf("floating point exception");
            }
        }
    }
}

int main(int ac, char *av[])
{
    // initializes the interactive variables for GNU History Library
    using_history();

    int opt;

    /* Process command-line arguments. See getopt(3) */
    while ((opt = getopt(ac, av, "h")) > 0)
    {
        switch (opt)
        {
        case 'h':
            usage(av[0]);
            break;
        }
    }

    list_init(&job_list);
    signal_set_handler(SIGCHLD, sigchld_handler);
    termstate_init();

    /* Read/eval loop. */
    for (;;)
    {

        /* Do not output a prompt unless shell's stdin is a terminal */
        char *prompt = isatty(0) ? build_prompt() : NULL;
        char *cmdline = readline(prompt);
        free(prompt);
        

        if (cmdline == NULL) /* User typed EOF */
            break;

        // Setting up the history list using the sample code given in the GNU Library 
        char *expansion;
        //int result;

        history_expand(cmdline, &expansion);
        //result = history_expand(cmdline, &expansion);
        // if (result)
        //     fprintf(stderr, "%s\n", expansion);

        // if (result < 0 || result == 2)
        // {
        //     free(expansion);
        //     continue;
        // }

        add_history(expansion);

        struct ast_command_line *cline = ast_parse_command_line(expansion);
        free(cmdline);
        if (cline == NULL) /* Error in command line */
            continue;

        if (list_empty(&cline->pipes))
        { /* User hit enter */
            ast_command_line_free(cline);
            continue;
        }
        struct list_elem *e = list_begin(&cline->pipes);
        for(; e != list_end(&cline -> pipes); e = list_next(e))
        {
            struct ast_pipeline *pipe = list_entry(e, struct ast_pipeline, elem);
            if(handle_builtin(pipe) == 0) continue;
            handle_job(pipe);

        }

        //ast_command_line_print(cline);      /* Output a representation of
        //                                        the entered command line */
        delete_dead_jobs();

        /* Free the command line.
         * This will free the ast_pipeline objects still contained
         * in the ast_command_line.  Once you implement a job list
         * that may take ownership of ast_pipeline objects that are
         * associated with jobs you will need to reconsider how you
         * manage the lifetime of the associated ast_pipelines.
         * Otherwise, freeing here will cause use-after-free errors.
         */
        free(cline);
    }
    return 0;
}

/**
 * \brief handle_job handles running the commands passed to the shell
 * the function adds a new jobn to the job list and updates it's pgid
 * The function handles fg and bg commands
 *
 * \param pipe list of commands of the job to be run
 * \return int 0 if add was successful, -1 of adding failed
 * \todo
 * add bg command support
 * replace waitpid with wait_for_job once Status is done
 *
 */

// failing fds when cmd_len = 0 ( running a normal command)
// even number  pipes?
int handle_job(struct ast_pipeline *pipe)
{
    
    bool leader = true;
    bool fail = false;
    pid_t pid;
    pid_t pgid = 0;
    int* fds = NULL;
    size_t cmd_len = list_size(&pipe->commands);
    size_t fd_len;
    if(cmd_len > 1) 
    {
        fd_len = (cmd_len-1)*2;
        fds = _create_pipes(fd_len);

    }
    
    else fd_len = 1;
    size_t itr = 0;
    

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa); // ensure it is never passed down to posix_spawnp unintialized

    signal_block(SIGCHLD); // Question:: when to unblock signal?

    struct job *curJob = add_job(pipe);
    struct list_elem *e = list_begin(&pipe->commands);
    //ast_pipeline_print(pipe);
    // iterate over all commands in a pipe
    for (; e != list_end(&pipe->commands); e = list_next(e))
    {
        posix_spawn_file_actions_init(&fa);
        struct ast_command *cmd = list_entry(e, struct ast_command, elem); // Question:: ask about elem

        // handles io redirecting for input file
        if(pipe->iored_input != NULL) 
        {
            if(_set_up_io(&fa, pipe -> iored_input, pipe ->append_to_output, cmd -> dup_stderr_to_stdout, IO_IN) != 0)
            {
                posix_spawn_file_actions_init(&fa);
                
            }

        }
        //handles io redirecting for output file
        if(pipe->iored_output != NULL)
        {
            if(_set_up_io(&fa,pipe -> iored_output, pipe ->append_to_output, cmd ->dup_stderr_to_stdout,  IO_OUT) != 0)
                posix_spawn_file_actions_init(&fa);


        }
        if(cmd_len > 1)
        {
            
            _set_up_pipe(&fa,fds,itr,cmd_len);
            itr++;
        } 
        if(cmd ->dup_stderr_to_stdout)
        {
            posix_spawn_file_actions_adddup2(&fa, STDOUT_FILENO, STDERR_FILENO);
        }
        //handles running fg and bg process
        if (_posix_spawn_run(&pid, pgid, cmd->argv, leader, !pipe->bg_job, &fa) != 0)
        {
            fail = true;
            delete_job(curJob);
            break;
        }  
        // run only after first command
        if (e == list_begin(&pipe->commands))
        {
            pgid = pid; // set the first process's pid to the pgid
            leader = false;
            curJob->pgid = pgid; // add group id to the job
        }
        cmd->pid = pid; // stores process pid in the command struct;
        curJob->num_processes_alive++;
    }
    _close_fds(fds, fd_len);
    // wait for all childern of pgid to exit
    if (!pipe->bg_job && !fail)
    {
        wait_for_job(curJob);
    }
    else
    {
        if(!fail)
        {
            printf("[%d] %d\n", curJob -> jid, curJob -> pgid);
        }
    }
    signal_unblock(SIGCHLD);
    posix_spawn_file_actions_destroy(&fa);
    
    termstate_give_terminal_back_to_shell();
    free(fds);
    return 0;
}

/**
 * \brief function creates pipes given the number of pipes needed
 *
 * \param num number of pipes to create
 * \return returns a pointer to an array storing the fds for the pipes
 * 
 *
 */

int* _create_pipes(int num)
{
    int * fds = malloc(num*sizeof(int));
    for(int i =0; i < num; i+=2)
    {
        pipe2(fds + i, O_CLOEXEC);
    }
    return fds;
}
/**
 * \brief sets up pipe configartions for process being run. an incoming process can have
 * one of three states:
 * - first command: output mapped to the write side of the pipe
 * -last command: input is mapped to the read side of the pipe
 * - middle commands: 
 *      - input is mapped the read side of the pipe with the preecding command
 *      - output is mapped to the write side of the pipe with the succeeding command 
 *
 * \param fa pointer to posix_spawn_file_actions holding process data
 * \param newfd pipefd
 * \param iteration the command index 
 * \param cmd_len number of commands in the pipeline
 * \return int 0 if it was successful, -1 of adding failed

 *
 */
int _set_up_pipe(posix_spawn_file_actions_t* fa, int* newfds, int iteration, int cmd_len)
{
    static int fd_index = 0; //index in teh newfds array
    
    //intial command
    if(iteration == 0)
    {
        posix_spawn_file_actions_adddup2(fa,   newfds[fd_index +1], STDOUT_FILENO);
    
    }
    //last command 
    else if(iteration == cmd_len -1)
    {
        posix_spawn_file_actions_adddup2(fa, newfds[fd_index], STDIN_FILENO);
        fd_index = 0;
    }
    //middle commands 
    else 
    {
        posix_spawn_file_actions_adddup2(fa,newfds[fd_index], STDIN_FILENO);
        fd_index+=2;
        posix_spawn_file_actions_adddup2(fa, newfds[fd_index+1], STDOUT_FILENO);
    }

    return 0;

}
/**
 * \brief close all file descriptors in a given array
 *
 * \param fds array of file descriptors to be closed
 * \param length length of the fds array
 * \return int 0 if close was successful, -1 of closing failed
 *
 */
int _close_fds(int* fds, size_t length)
{
    if(fds == NULL) return 0;
    for(int i = 0; i < length; i++)
    {
        close(fds[i]);
    }
    return 0;
}
/**
 * \brief handles setting up io redirection by creating a posix_Spawn_file_actions struct.
 * supports 2 modes set by the flags:
 *  - IO_IN for using a file as input
 * - IO_OUT for using file as output
 * 
 * \param fa pointer to posix_spawn_file_actions struct
 * \param iored file name that will be used as input or output
 * \param __ioflag type of redirection
 * \return int return 0 if esetup was succesful & -1 if an error occured 
 * \todo remove duo_stdout_to_stdin already done on handle_job
 */
int _set_up_io( posix_spawn_file_actions_t* fa, char* iored, int append, int dup_stdout_to_stdin, int __ioflag)
{
    int err = 0;
    // <
    if(__ioflag == IO_IN)
    {
         err = posix_spawn_file_actions_addopen(fa, STDIN_FILENO, iored, O_RDONLY, 0);
        if(err != 0) printf("Could not open %s, reading from stdin No such file or directory", iored);

    }
    
    // >
    else if (__ioflag == IO_OUT) 
    {
        if(append)
        {
           err= posix_spawn_file_actions_addopen(fa, STDOUT_FILENO, iored, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP);
           

        }
        else
        {
            err = posix_spawn_file_actions_addopen(fa, STDOUT_FILENO, iored, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP);
            
        }
        //posix_spawn_file_actions_addclose(fa, STDOUT_FILENO); 
    }
   
    if(dup_stdout_to_stdin)
    {
        posix_spawn_file_actions_adddup2(fa, STDOUT_FILENO, STDERR_FILENO);
    }
    
    return err;

}

/**
 * \brief manages running a fg process with argv. Sets up all the needed attr to run
 * the process. Sets up process group to get obtain terminal control.
 * Addeds the process it runs to the pgid given
 *
 * \param pid pointer to variable that will store process pid
 * \param pgid group process id of the process's group
 * \param argv arguments for the process
 * \param leader true if process is group leader
 * \param fg true if process is to be run in fg and false if to be run in bg
 * \return int 0 if process is succesfully run, -1 if process fails
 *
 */
int _posix_spawn_run(pid_t *pid, pid_t pgid, char **argv, bool leader, bool fg, posix_spawn_file_actions_t* fa)
{
    posix_spawnattr_t attr;
    // intialize poxis_spawn attributes
    if (posix_spawnattr_init(&attr) != 0)
        printf("posix init error \n");

    if (leader && fg)
    {
        // set flags for getting terminal acess & setting a group process
        if (posix_spawnattr_setflags(&attr, POSIX_SPAWN_TCSETPGROUP | POSIX_SPAWN_SETPGROUP) != 0)
        {
            printf("posix flag error");
            return -1;
        }
        // updates attr with a fd controlling terminal
        if (posix_spawnattr_tcsetpgrp_np(&attr, termstate_get_tty_fd()) != 0)
        {
            printf("posix terminal control error\n");
            return -1;
        }
    }
    else
    {
        // sets flags to set process's group
        if (posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP) != 0)
        {
            printf("posix flag error");
            return -1;
        }
    }
    // updates attr with the pgid the process will be added to
    // if 0 creates a new process group
    if (posix_spawnattr_setpgroup(&attr, pgid) != 0)
    {
        printf("posix set group");
        return -1;
    }
    // runs the process if return != 0 the process failed to run
    
    if (posix_spawnp(pid, argv[0], fa, &attr, argv, environ) != 0)
    {
        printf("%s: No such file or directory\n", argv[0]);
        return -1;
    }

    return 0;
}

/**
 * \brief Handles builtin commands. handles kill,fg,bg,stop,exit commands
 * -kill terminates a process given the jid
 * -fg brings a background or stopped job to the foreground given jid
 * -bg starts a stopped job into the background given jid
 * -stop stops a running job given the jid
 * -exit exits out of the program
 * 
 * \param pipe 
 * \return int 0 if it's a bultin command or -1 if it's not
 * \todo clean up bg command, add exit command
 */
int handle_builtin(struct ast_pipeline *pipe)
{
    struct ast_command *commands = list_entry(list_begin(&pipe->commands), struct ast_command, elem);
    if (strcmp("jobs", commands->argv[0]) == 0)
    {
        for (int i = 0; i < MAXJOBS; i++)
        {
            if (jid2job[i] == NULL)
                continue;
            print_job(jid2job[i]);
        }
        return 0;
    }
    else if (strcmp("kill", commands->argv[0]) == 0)
    {
        // no jid provided
        if (commands->argv[1] == NULL)
        {
            printf("kill: job id missing\n");
            return 0;
        }
        int jid = atoi(commands->argv[1]); // convert char* to int
        // job doesn't exists
        if (get_job_from_jid(jid) == NULL)
        {
            printf("kill %s: No such job\n", commands->argv[1]);
            return 0;
        }
        killpg(get_job_from_jid(jid)->pgid, SIGTERM); // sends terminate signal
    }
    else if (strcmp("stop", commands->argv[0]) == 0)
    {
        if (commands->argv[1] == NULL)
        {
            printf("%s: job id missing\n", commands->argv[0]);
            return 0;
        }
        int jid = atoi(commands->argv[1]);
        if (get_job_from_jid(jid) == NULL)
        {
            printf("%s %s: No such job\n", commands->argv[0], commands->argv[1]);
            return 0;
        }
        killpg(get_job_from_jid(jid)->pgid, SIGTSTP); // send stop signal
    }
    else if (strcmp("fg", commands->argv[0]) == 0)
    {

        if (commands->argv[1] == NULL)
        {
            printf("%s: job id missing\n", commands->argv[0]);
            return 0;
        }
        int jid = atoi(commands->argv[1]);
        if (get_job_from_jid(jid) == NULL)
        {
            printf("%s %s: No such job\n", commands->argv[0], commands->argv[1]);
            return 0;
        }
        struct job *job = get_job_from_jid(jid);
        print_cmdline(job->pipe); // print pipeline begining sent to the fg
        printf("\n");

        job->status = FOREGROUND; // update job status

        killpg(job->pgid, SIGCONT); // sends continue signal in case the process is stopped
        termstate_give_terminal_to(NULL, job->pgid);
        signal_block(SIGCHLD);
        wait_for_job(job);
        signal_unblock(SIGCHLD);
        termstate_give_terminal_back_to_shell();
    }
    // needs cleaning up
    else if (strcmp("bg", commands->argv[0]) == 0)
    {
        int jid = atoi(commands->argv[1]);
        struct job *job = get_job_from_jid(jid);
        job->status = BACKGROUND;
        killpg(get_job_from_jid(jid)->pgid, SIGCONT);
    }
    // history command
    else if (strcmp("history", commands->argv[0]) == 0)
    {
        //gets the history list that includes the commands
        HIST_ENTRY** historyList = history_list();
        for (int i = 0; i < history_length; i++)
        {
            //printing the commands
            printf("    %d %s\n", i, historyList[i]->line);
        }
    }
    else if(strcmp("exit", commands ->argv[0]) == 0)
    {
        delete_dead_jobs();
        exit(EXIT_SUCCESS);
    }
    else
    {
        return -1;
    }
    return 0;
}
/**
 * \brief interates over the job list and deletes any job that has no processes alive
 *
 */
void delete_dead_jobs()
{
    for (int i = 0; i < MAXJOBS; i++)
    {
        if (jid2job[i] == NULL)
        {
            continue;
        }
        if (jid2job[i]->num_processes_alive == 0)
        {
            if(jid2job[i] -> status == DONE) printf("[%d]\t%s\t\t\n", jid2job[i]->jid, get_status(jid2job[i]->status));;
            delete_job(jid2job[i]);
        }
    }
}




