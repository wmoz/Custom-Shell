/*
 * cush - the customizable shell.
 *
 * Developed by Godmar Back for CS 3214 Summer 2020 
 * Virginia Tech.  Augmented to use posix_spawn in Fall 2021.
 */
#define _GNU_SOURCE    1
#include <stdio.h>
#include <readline/readline.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <sys/wait.h>
#include <assert.h>
#include <spawn.h>

/* Since the handed out code contains a number of unused functions. */
#pragma GCC diagnostic ignored "-Wunused-function"

#include "termstate_management.h"
#include "signal_support.h"
#include "shell-ast.h"
#include "utils.h"

extern char **environ;
int handle_job(struct ast_pipeline* pipe);
int _posix_fg(pid_t *pid, pid_t pgid, char** argv, bool leader, bool fg);
static void handle_child_status(pid_t pid, int status);

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
    return strdup("cush> ");
}

enum job_status {
    FOREGROUND,     /* job is running in foreground.  Only one job can be
                       in the foreground state. */
    BACKGROUND,     /* job is running in background */
    STOPPED,        /* job is stopped via SIGSTOP */
    NEEDSTERMINAL,  /* job is stopped because it was a background job
                       and requires exclusive terminal access */
};

struct job {
    struct list_elem elem;   /* Link element for jobs list. */
    struct ast_pipeline *pipe;  /* The pipeline of commands this job represents */
    int     jid;             /* Job id. */
    enum job_status status;  /* Job status. */ 
    int  num_processes_alive;   /* The number of processes that we know to be alive */
    struct termios saved_tty_state;  /* The state of the terminal when this job was 
                                        stopped after having been in foreground */
   
    /* Add additional fields here if needed. */
    pid_t pgid; //stores the pgid for the job


};

/* Utility functions for job list management.
 * We use 2 data structures: 
 * (a) an array jid2job to quickly find a job based on its id
 * (b) a linked list to support iteration
 */
#define MAXJOBS (1<<16)
static struct list job_list;

static struct job * jid2job[MAXJOBS];

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
    struct job * job = malloc(sizeof *job);
    job->pipe = pipe;
    job->num_processes_alive = 0;
    list_push_back(&job_list, &job->elem);
    for (int i = 1; i < MAXJOBS; i++) {
        if (jid2job[i] == NULL) {
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
    switch (status) {
    case FOREGROUND:
        return "Foreground";
    case BACKGROUND:
        return "Running";
    case STOPPED:
        return "Stopped";
    case NEEDSTERMINAL:
        return "Stopped (tty)";
    default:
        return "Unknown";
    }
}

/* Print the command line that belongs to one job. */
static void
print_cmdline(struct ast_pipeline *pipeline)
{
    struct list_elem * e = list_begin (&pipeline->commands); 
    for (; e != list_end (&pipeline->commands); e = list_next(e)) {
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

    while ((child = waitpid(-1, &status, WUNTRACED|WNOHANG)) > 0) {
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

    while (job->status == FOREGROUND && job->num_processes_alive > 0) {
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
    
    

}

int
main(int ac, char *av[])
{
    int opt;
    
    
    

    /* Process command-line arguments. See getopt(3) */
    while ((opt = getopt(ac, av, "h")) > 0) {
        switch (opt) {
        case 'h':
            usage(av[0]);
            break;
        }
    }

    list_init(&job_list);
    signal_set_handler(SIGCHLD, sigchld_handler);
    termstate_init();
   

    /* Read/eval loop. */
    for (;;) {

        /* Do not output a prompt unless shell's stdin is a terminal */
        char * prompt = isatty(0) ? build_prompt() : NULL;
        char * cmdline = readline(prompt);
        free (prompt);

        if (cmdline == NULL)  /* User typed EOF */
            break;

        struct ast_command_line * cline = ast_parse_command_line(cmdline);
        free (cmdline);
        if (cline == NULL)                  /* Error in command line */
            continue;

        if (list_empty(&cline->pipes)) {    /* User hit enter */
            ast_command_line_free(cline);
            continue;
        }
        struct list_elem * e = list_begin (&cline->pipes);
        struct ast_pipeline *pipe = list_entry(e, struct ast_pipeline, elem);
        handle_job(pipe);
        // ast_command_line_print(cline);      /* Output a representation of
        //                                        the entered command line */

        /* Free the command line.
         * This will free the ast_pipeline objects still contained
         * in the ast_command_line.  Once you implement a job list
         * that may take ownership of ast_pipeline objects that are
         * associated with jobs you will need to reconsider how you
         * manage the lifetime of the associated ast_pipelines.
         * Otherwise, freeing here will cause use-after-free errors.
         */
        ast_command_line_free(cline);


        
        


        
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
 * replace waitpid with wait_for_job once handle_child_Status is done
 * 
 */
int handle_job(struct ast_pipeline* pipe)
{
    bool leader = true;
    pid_t pid;
    pid_t pgid =0;

    signal_block(SIGCHLD); //Question:: when to unblock signal?

    struct job* curJob = add_job(pipe);
    struct list_elem * e = list_begin (&pipe->commands);

    //iterate over all commands in a pipe 
    for (; e != list_end (&pipe->commands); e = list_next(e)) 
    {
        struct ast_command *cmd = list_entry(e, struct ast_command, elem); // Question:: ask about elem
        
        //handles running fg and bg process
        if(_posix_spawn_run(&pid,pgid, cmd -> argv,leader, !pipe->bg_job) != 0) return -1; //Question:: what happens if a command fails
        
        
       

        //run only after first command
        if (e == list_begin(&pipe->commands)){
            pgid = pid; //set the first process's pid to the pgid
            leader = false;
            curJob -> pgid = pgid; // add group id to the job
        } 
        curJob-> num_processes_alive++;

    }
    //wait for all childern of pgid to exit
    if(!pipe -> bg_job)
    {
        if(waitpid(-pgid, NULL, 0) == -1) // replace with wait_for_job when handle_child_status is done 
        {
        }
    }
    
    termstate_give_terminal_back_to_shell();
    return 0;

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
int _posix_spawn_run(pid_t *pid, pid_t pgid, char** argv, bool leader, bool fg)
{
    posix_spawnattr_t attr;
    //intialize poxis_spawn attributes
    if(posix_spawnattr_init (&attr) != 0) 
        printf("posix init error \n");

    
    if(leader && fg)
    {
        //set flags for getting terminal acess & setting a group process
        if(posix_spawnattr_setflags (&attr, POSIX_SPAWN_TCSETPGROUP | POSIX_SPAWN_SETPGROUP )!=0) 
        {
            printf("posix flag error");
            return -1;
        }
        //updates attr with a fd controlling terminal 
        if(posix_spawnattr_tcsetpgrp_np(&attr,termstate_get_tty_fd())!=0)
        {
            printf("posix terminal control error\n");
            return -1;
        }
    }
    else
    {  
        //sets flags to set process's group
        if(posix_spawnattr_setflags (&attr, POSIX_SPAWN_SETPGROUP)!=0) 
        {
            printf("posix flag error");
            return -1;
        }
    }
    //updates attr with the pgid the process will be added to
    //if 0 creates a new process group
    if(posix_spawnattr_setpgroup(&attr,pgid)!= 0)
    {
        printf("posix set group");
        return -1;
    }
    //runs the process if return != 0 the process failed to run
    if(posix_spawnp(pid, argv[0], NULL, &attr, argv, environ) != 0)
    {
        printf("%s: No such file or directory\n", argv[0]);
        return -1;
    }

    return 0;

}
// int _posix_bg()
// {

// }



        // terminal state
        // running command; command
        // initially step // does it matter which process is run first in a pipe
        //signals 