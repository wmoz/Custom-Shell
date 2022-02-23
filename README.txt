Student Information
-------------------
Walid Zeineldin (wmoz)
Omar El-geoushy (omarelgeoushy)

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
\^C *omarelgeoushy*

\^Z *omarelgeoushy*



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
    -middle processes:
        - input is mapped the read side of the pipe with the write side connected to the next process's 
        output
        - output is mapped to the write side of the pipe with the read side connected to the next process's
        input
    last process: 
        - input is mapped the read side of the pipe with the write side connected to the next process's 
        output

Exclusive Access *omarelgeoushy*


List of Additional Builtins Implemented *omarelgeoushy*
---------------------------------------
(Written by Your Team)
<builtin name>
<description>
