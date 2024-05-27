/*******************************************************************************
 *  The BYTE UNIX Benchmarks - Release 3
 *          Module: syscall.c   SID: 3.3 5/15/91 19:30:21
 *
 *******************************************************************************
 * Bug reports, patches, comments, suggestions should be sent to:
 *
 *	Ben Smith, Rick Grehan or Tom Yager at BYTE Magazine
 *	ben@bytepb.byte.com   rick_g@bytepb.byte.com   tyager@bytepb.byte.com
 *
 *******************************************************************************
 *  Modification Log:
 *  $Header: syscall.c,v 3.4 87/06/22 14:32:54 kjmcdonell Beta $
 *  August 29, 1990 - Modified timing routines
 *  October 22, 1997 - code cleanup to remove ANSI C compiler warnings
 *                     Andy Kahn <kahn@zk3.dec.com>
 *
 ******************************************************************************/
/*
 *  syscall  -- sit in a loop calling the system
 *
 */
char SCCSid[] = "@(#) @(#)syscall.c:3.3 -- 5/15/91 19:30:21";

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "timeit.c"
#include <string.h>
#include <fcntl.h>
#include <liburing.h>

#define QUEUE_DEPTH 8

unsigned long iter;

/*
 * Submit the close request via liburing
 * */
int submit_close_request(int file_fd, struct io_uring *ring) {
	struct io_uring_cqe *cqe;

	for (int i = 0; i < QUEUE_DEPTH; i++) {
		/* Get an SQE */
		struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
		/* Setup a close operation */
		io_uring_prep_close(sqe, dup(file_fd));
	}
	
	io_uring_submit(ring);

	for (int i = 0; i < QUEUE_DEPTH; i++) {
        	int ret = io_uring_wait_cqe(ring, &cqe);
        	if (ret < 0) {
            		fprintf(stderr, "Error waiting for completion: %s\n",
                 	                                           strerror(-ret));
            		return 1;
        	}
        	/* Now that we have the CQE, let's process the data */
        	if (cqe->res < 0) {
            		fprintf(stderr, "Error in async operation: %s\n", strerror(-cqe->res));
        	}
        	//printf("Result of the operation: %d\n", cqe->res);
		io_uring_cqe_seen(ring, cqe);
    	}

	return 0;
}

void report()
{
	fprintf(stderr,"COUNT|%ld|1|lps\n", iter);
	exit(0);
}

int create_fd()
{
	int fd[2];

	if (pipe(fd) != 0 || close(fd[1]) != 0)
	    exit(1);

	return fd[0];
}

int main(argc, argv)
int	argc;
char	*argv[];
{
        char   *test;
	int	duration;
	int	fd, ret;
	struct io_uring ring;

	if (argc < 2) {
		fprintf(stderr,"Usage: %s duration [ test ]\n", argv[0]);
                fprintf(stderr,"test is one of:\n");
                fprintf(stderr,"  \"mix\" (default), \"close\", \"getpid\", \"exec\"\n");
		exit(1);
	}
        if (argc > 2)
            test = argv[2];
        else
            test = "mix";

	duration = atoi(argv[1]);

	/* Initialize io_uring */
	ret = io_uring_queue_init(QUEUE_DEPTH, &ring, 0);
	if (ret < 0) {
		fprintf(stderr, "queue_init: %s\n", strerror(-ret));
		return 1;
	}

	ret = io_uring_register_ring_fd(&ring);
        if (ret < 0) {
                fprintf(stderr, "register_ring_fd: %s\n", strerror(-ret));
                return 1;
        }
        ret = io_uring_close_ring_fd(&ring);
        if (ret < 0) {
                fprintf(stderr, "close_ring_fd: %s\n", strerror(-ret));
                return 1;
        }

	iter = 0;
	wake_me(duration, report);

        switch (test[0]) {
        case 'm':
	   fd = create_fd();
	   while (1) {
		close(dup(fd));
		syscall(SYS_getpid);
		getuid();
		umask(022);
		iter++;
	   }
	   /* NOTREACHED */
        case 'c':
           fd = create_fd();
           while (1) {
                //close(dup(fd));
                submit_close_request(fd, &ring);
                //iter++;
                iter+=QUEUE_DEPTH;
           }
           /* NOTREACHED */
        case 'g':
           while (1) {
                syscall(SYS_getpid);
                iter++;
           }
           /* NOTREACHED */
        case 'e':
           while (1) {
                pid_t pid = fork();
                if (pid < 0) {
                    fprintf(stderr,"%s: fork failed\n", argv[0]);
                    exit(1);
                } else if (pid == 0) {
                    execl("/bin/true", "/bin/true", (char *) 0);
                    fprintf(stderr,"%s: exec /bin/true failed\n", argv[0]);
                    exit(1);
                } else {
                    if (waitpid(pid, NULL, 0) < 0) {
                        fprintf(stderr,"%s: waitpid failed\n", argv[0]);
                        exit(1);
                    }
                }
                iter++;
           }
           /* NOTREACHED */
        }

        exit(9);
}

