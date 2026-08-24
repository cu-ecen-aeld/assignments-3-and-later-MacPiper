#include "systemcalls.h"
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
bool do_system(const char *cmd)
{

/*
 * TODO:
 *  Call the system() function with the command set in the cmd
 *   and return a boolean true if the system() call completed with success
 *   or false() if it returned a failure
*/
    unsigned int retVal = system(cmd);
    return (retVal == 0);    
}

/**
* @param count -The numbers of variables passed to the function. The variables are command to execute.
*   followed by arguments to pass to the command
*   Since exec() does not perform path expansion, the command to execute needs
*   to be an absolute path.
* @param ... - A list of 1 or more arguments after the @param count argument.
*   The first is always the full path to the command to execute with execv()
*   The remaining arguments are a list of arguments to pass to the command in execv()
* @return true if the command @param ... with arguments @param arguments were executed successfully
*   using the execv() call, false if an error occurred, either in invocation of the
*   fork, waitpid, or execv() command, or if a non-zero return value was returned
*   by the command issued in @param arguments with the specified arguments.
*/

bool do_exec(int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;

/*
 * TODO:
 *   Execute a system command by calling fork, execv(),
 *   and wait instead of system (see LSP page 161).
 *   Use the command[0] as the full path to the command to execute
 *   (first argument to execv), and use the remaining arguments
 *   as second argument to the execv() command.
 *
*/

    bool result = false;
    int wait_status;
    pid_t pid = fork();
    if (pid == 0) {
        // child process part
        execv(command[0], command);
        // only reached in error case
        perror("execv");
        _exit(127); // common convention: exec failed
    } else if (pid == -1) {
        // parent process part with fork failed
        perror("fork");
    } else {
        // parent process part with fork passed
        wait(&wait_status);
        if (WIFEXITED(wait_status)) {
            int exitStatus = WEXITSTATUS(wait_status);
            if (exitStatus == 0) {
                result = true;
            } else {
                fprintf(stderr, "Child exited with status %d\n", exitStatus);
            }

        } else {
            fprintf(stderr,"Wait or the process that was waited for failed");
        }
    }

    va_end(args);
    return result;
}

/**
* @param outputfile - The full path to the file to write with command output.
*   This file will be closed at completion of the function call.
* All other parameters, see do_exec above
*/
bool do_exec_redirect(const char *outputfile, int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
    // this line is to avoid a compile warning before your implementation is complete
    // and may be removed
    command[count] = command[count];


/*
 * TODO
 *   Call execv, but first using https://stackoverflow.com/a/13784315/1446624 as a refernce,
 *   redirect standard out to a file specified by outputfile.
 *   The rest of the behaviour is same as do_exec()
 *
*/

    bool result = false;
    int wait_status;
    int fd = open(outputfile, O_WRONLY|O_TRUNC|O_CREAT, 0644); 
    if (fd < 0) { 
        perror("file open"); 
        return false; 
    }
    pid_t pid = fork();
    if (pid == 0) {
        // duplicate the file descriptor for outputfile we got from parent into stdout
        if (dup2(fd, 1) < 0) {
            perror("dup2"); 
            _exit(127); 
        } 
        close(fd); // this closes the outputfile filedescriptor 
        // child process part
        execv(command[0], command);
        // only reached in error case
        perror("execv");
        _exit(127); // common convention: exec failed
    } else if (pid == -1) {
        // parent process part with fork failed
        perror("fork");
    } else {
        // parent process part with fork passed
        wait(&wait_status);
        close(fd);
        if (WIFEXITED(wait_status)) {
            int exitStatus = WEXITSTATUS(wait_status);
            if (exitStatus == 0) {
                result = true;
            } else {
                fprintf(stderr, "Child exited with status %d\n", exitStatus);
            }

        } else {
            fprintf(stderr,"Wait or the process that was waited for failed");
        }
    }

    va_end(args);
    return result;

}