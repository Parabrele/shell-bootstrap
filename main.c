#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>

#include "global.h"

// This is the file that you should work on.

// declaration
int execute(struct cmd *cmd);

// name of the program, to be printed in several places
#define NAME "myshell"

// Some helpful functions

void errmsg(char *msg)
{
	fprintf(stderr, "error: %s\n", msg);
}

// apply_redirects() should modify the file descriptors for standard
// input/output/error (0/1/2) of the current process to the files
// whose names are given in cmd->input/output/error.
// append is like output but the file should be extended rather
// than overwritten.

void apply_redirections(struct cmd *cmd)
{
	if (cmd->input)
	{
		int fd = open(cmd->input, O_RDONLY);
		if (fd == -1)
		{
			errmsg("error opening input file");
			exit(-1);
		}

		if (dup2(fd, STDIN_FILENO) < 0)
		{
			errmsg("cannot redirect input");
			return;
		}
		close(fd);
	}

	if (cmd->output)
	{
		// 0644 est la permission
		int fd = open(cmd->output, O_WRONLY | O_TRUNC | O_CREAT, 0644);
		if (fd == -1)
		{
			errmsg("error opening output file");
			exit(-1);
		}
		if (dup2(fd, STDOUT_FILENO) < 0)
		{
			errmsg("cannot redirect output");
			return;
		}
		close(fd);
	}

	if (cmd->append)
	{
		int fd = open(cmd->append, O_WRONLY | O_APPEND | O_CREAT, 0644);
		if (fd == -1)
		{
			errmsg("error opening output file");
			exit(-1);
		}
		if (dup2(fd, STDOUT_FILENO) < 0)
		{
			errmsg("cannot redirect output");
			return;
		}
		close(fd);
	}

	if (cmd->error)
	{
		int fd = open(cmd->error, O_WRONLY | O_TRUNC | O_CREAT, 0644);
		if (fd == -1)
		{
			errmsg("error opening error file");
			exit(-1);
		}
		if (dup2(fd, STDERR_FILENO) < 0)
		{
			errmsg("cannot redirect error");
			return;
		}
		close(fd);
	}
}

// The function execute() takes a command parsed at the command line.
// The structure of the command is explained in output.c.
// Returns the exit code of the command in question.

int execute(struct cmd *cmd)
{
	switch (cmd->type)
	{
	case C_PLAIN:
	{
		// handle the "cd" command
		if (strcmp(cmd->args[0], "cd") == 0) // string comparison
		{
			// change the current working directory
			if (cmd->args[1] == NULL)
			{
				// no directory specified, go to home directory
				char *home_dir = getenv("HOME");
				if (chdir(home_dir) != 0)
				{
					errmsg("an error occurred while changing the current working directory");
					return -1;
				}
			}
			else
			{
				// change to the specified directory
				if (chdir(cmd->args[1]) != 0)
				{
					errmsg("an error occurred while changing the current working directory");
					return -1;
				}
			}
			// return success
			return 0;
		}

		apply_redirections(cmd);

		int pid = fork();
		if (pid == 0)
		{
			// This is the child process.
			// execvp() is preferred over other exec functions because it returns an error if the specified program is not found or if there is an error executing the program, making it easier to handle these cases.

			execvp(cmd->args[0], cmd->args);

			fprintf(stderr, "exec() failed: %s\n", strerror(errno));
			exit(1);
		}
		else
		{
			// This is the parent process.
			int status;
			waitpid(pid, &status, 0);
			if (WIFEXITED(status))
			{
				// return the exit status of the command
				return WEXITSTATUS(status);
			}

			else
			{
				// return -1 if the command did not terminate normally
				return -1;
			}
		}
	}

	case C_SEQ:
	{
		int exit_code = execute(cmd->left);
		if (exit_code != 0)
		{
			errmsg("Error executing left command in C_SEQ case");
		}

		exit_code = execute(cmd->right);
		if (exit_code != 0)
		{
			errmsg("Error executing right command in C_SEQ case");
		}

		return exit_code;
	}
	case C_AND:
	{
		// execute the left command
		int left_status = execute(cmd->left);
		// if the left command succeeds (exit code of 0), execute the right command
		if (left_status == 0)
			return execute(cmd->right);
		// otherwise, return the exit code of the left command
		else
			return left_status;
	}
	case C_OR:
	{
		int exit_code = execute(cmd->left);
		if (exit_code != 0)
		{
			return execute(cmd->right);
		}
		else
		{
			return exit_code;
		}
	}
	
	case C_PIPE:
	{
		// Create a pipe
		int fd[2];
		if (pipe(fd) == -1)
		{
			errmsg("The plumber was busy working on another pipe.");
			exit(1);
		}

		// Create a child process for the left command
		int pid_left = fork();
		if (pid_left == 0)
		{
			// Child process for the left command
			// Redirect standard output to the write end of the pipe
			if (dup2(fd[1], STDOUT_FILENO) == -1)
			{
				errmsg("Error during output deportation in pipe.");
				exit(1);
			}

			// Close the read end of the pipe
			close(fd[0]);

			// Execute the left command
			exit(execute(cmd->left));
		}
		else if (pid_left > 0)
		{
			// Parent process
			// Create a child process for the right command
			int pid_right = fork();
			if (pid_right == 0)
			{
				// Child process for the right command
				// Redirect standard input to the read end of the pipe
				if (dup2(fd[0], STDIN_FILENO) == -1)
				{
					errmsg("Error during input deportation in pipe.");
					exit(1);
				}

				// Close the write end of the pipe
				close(fd[1]);

				// Execute the right command
				exit(execute(cmd->right));
			}
			else if (pid_right > 0)
			{
				// Parent process
				// Close both ends of the pipe
				close(fd[0]);
				close(fd[1]);

				// Wait for both child processes to finish
				int status_left, status_right;
				waitpid(pid_left, &status_left, 0);
				waitpid(pid_right, &status_right, 0);

				// Return the exit status of the right command
				return WEXITSTATUS(status_right);
			}
			else
			{
				// fork() failed
				errmsg("This fork is not a snail fork.");
				exit(1);
			}
		}
		else
		{
			// fork() failed
			errmsg("This fork is not a snail fork.");
			exit(1);
		}
	}
	case C_VOID:
	{
		apply_redirections(cmd);

		// execute left command, in parentheses
		execute(cmd->left);

		return 0;
	}
		errmsg("I do not know how to do this, please help me!");
		return -1;
	}

	// Just to satisfy the compiler
	errmsg("This cannot happen!");
	return -1;
}

int main(int argc, char **argv)
{
	char *prompt = malloc(strlen(NAME) + 3);
	printf("welcome to %s!\n", NAME);
	sprintf(prompt, "%s> ", NAME);

	while (1)
	{
		char *line = readline(prompt);
		if (!line)
			break; // user pressed Ctrl+D; quit shell
		if (!*line)
			continue; // empty line

		add_history(line); // add line to history

		struct cmd *cmd = parser(line);
		if (!cmd)
			continue; // some parse error occurred; ignore
		// output(cmd,0);	// activate this for debugging
		execute(cmd);
	}

	printf("goodbye!\n");
	return 0;
}
