Student Information
-------------------
Walid Zeineldin (wmoz)
Omar Elgeoushy (omarelgeoushy)

How to execute the shell
------------------------
To run the shell from the commandline run ./cush 

Important Notes
---------------


Description of Base Functionality
---------------------------------
jobs
Running the jobs command prints all active jobs 
This is achieved by interating over the array of undead jobs and printing out
there job id, status and Pipes
Ex:
[1]     Running         (sleep 100)

fg <job id>
The fg command takes in a stopped or backgrounded job's id and sends that job to the foreground
This is achieved by:
    - retriving the job from the job's id
    - sending out a SIGCONT to run the job if it was stopped
    - giving terminal control to that job
    - the shell waits for any signals from the job to update it's status and get back terminal control

bg <job id>
The bg command takes in a stopped job's id and runs it.
This is achieved by:
    - retriving the job from the job's id
    - sending a SIGCONT to the job to signal it to start running again 

kill <job id>
The kill command takes in a job's id and terminates it
This is achieved by:
    - retriving the job from the job's id
    - Sending a SIGTERM signal to the that job

stop <job id>
The stop command takes in a jobs's id and stops the job
This is achieved by:
    - retriving the job from the job's id
    - sends a SIGSTPT signal to stop that job

\^C
User terminates process
This is achieved by:
    - Sending signals which is being handled by handle_child_status()
        - Where handle_child_status() uses termstate_give_terminal_back_to_shell() if the process to be terminated is in the foreground
        - And decrement the number of processes alive
    - The signal sent is SIGINT which is being checked through WTERMSIG
    - Another check is WIFSIGNALED()

\^Z 
User stops fg process
This is achieved by:
    - Sending signals which is being handled by handle_child_status()
        - Where handle_child_status() uses termstate_give_terminal_back_to_shell() if the process to be terminated is in the foreground
        - The process is also saved in case it is required to resume the process using termstate_save()
        - The signal is also updated to be stopped
    -  The signal sent is SIGTSTP which is being checked through WSTOPSIG
    - Another check is WIFSTOPPED()


Description of Extended Functionality
-------------------------------------
I/O
The shell supports >,<,>>
    >
        - overwrite stdout and maps it to a new file. The redirection overwrites the file contents.
        - this is achieved by:
            - opening the file on the right of '>' as stdout
                - with the following flags:
                    - O_WRONLY opens the file in write mode to accept input from the left side of the '>'
                    - O_CREAT creates the file if it doesn't exist 
                    - O_TRUNC overwrite previous file contents
    >>
        - overwrite stdout and maps to a new file. The redirection appends to the files contents.
        - This is achived by:
            - opening the file on the right of '>>' as stdout
                - with the following flags:
                    - O_WRONLY opens the file in write mode to accept input from the left side of the '>'
                    - O_CREAT creates the file if it doesn't exist 
                    - O_APPEND appends to the file's contents


    <
        - overwrites stdin
        - this is achieved by:
            - opening the file on the right of '<' as stdin
                - with the following flag:
                    - O_RDONLY to read the files contents

Pipes

Piping allowa data to transfer between different processes 
Pipes were divided into three categories:
    - first process:
        - output mapped to the write side of the pipe with the read side connected to the next process's
        input
    - middle processes:
        - input is mapped the read side of the pipe with the write side connected to the next process's 
        output
        - output is mapped to the write side of the pipe with the read side connected to the next process's
        input
    last process: 
        - input is mapped the read side of the pipe with the write side connected to the next process's 
        output

Exclusive Access

This is handled in the handle_child_status() by making sure the terminal state is managed correctly 
and that the jobs are also saved in case we need to resume the process
    - First case (WIFSTOPPED):
        - If the user stops the process using for example ctrl-z, kill -STOP, etc.
        - Terminal is given back to the shell if in the foreground 
            - and the process is saved in case we need to resume the process
        - Job status is also updated according to how the user stopped
            - ex. STOPPED if was exited through ctrl-z
    - Second case (WIFEXITED):
        - If the user exits via exit()
        - Terminal is given back to the shell if the number of processes alive are 1 if in the foreground
        - If the process is in the background job status is updated to DONE 
        - Lastly the number of process is decremented
    - Third case (WIFSIGNALED):
        - If the user terminates the process or the process has been terminated (general case)
        - Terminal is given back to the shell if the number of processes alive are 1 if in the foreground
        - The number of process is decremented
        - Lastly, based on the signal passed a custom message is printed to the terminal



List of Additional Builtins Implemented
---------------------------------------
A custom prompt
    - The prompt to be shown on the terminal simular to PS1.
    - Was implemented using functions that return the username, hostname and the current directory
        - to get the hostname, gethostname() was used
        - to get the username, a passwd struct was used
            - getpwuid() was used to return the user database by passing the id of the user using getuid()
    - All that information is then stored into a buffer and then print

Command-line history
    - The 'history' command prints out the list of commands previously typed into the terminal
    - This builtin was implemented using the GNU History library as it helps efficently implement the builtin
        - First the interactive variables were initialized using using_history()
        - Later as guided in the sample code given by the man page of the GNU History Library an expansion variable was declared
          to be able to expand the history 
            - this was done using the history_expand() function by passing the expansion variable and the cmdline given to the terminal 
        - add_history() was then used to add the command to the list
        - A main change was also made to allow the give away implementations to work (!!, !n) was changing the variable passed into ast_parse_command_line()
          by passing the expansion instead of the cmdline
        - In the handle_builtin() anthor if statment was added to make sure we actually print the history
            - the if statment checked if the passed command was 'history' 
            - After that the history was obtained and stored it into a temp variable of type HIST_ENTRY** by calling history_list()
            - Later an iteration inside that list was made through a for loop to print every command in the list with the number of command linked to it
              ex. if 'history' is typed it will return
                '0 history'
