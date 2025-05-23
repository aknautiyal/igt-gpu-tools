#ifndef RUNNER_EXECUTOR_H
#define RUNNER_EXECUTOR_H

#include "job_list.h"
#include "settings.h"

struct execute_state
{
	size_t next;
	/*
	 * < 0 : No overall timeout used.
	 * = 0 : Timeouted, don't execute any more.
	 * > 0 : Timeout in use, time left.
	 */
	double time_left;
	bool dry;
};

enum {
	_F_JOURNAL,
	_F_OUT,
	_F_ERR,
	_F_DMESG,
	_F_SOCKET,
	_F_LAST,
};

bool open_output_files(int dirfd, int *fds, bool write);
bool open_output_files_rdonly(int dirfd, int *fds);
void close_outputs(int *fds);
const char *get_out_filename(int fid);

/*
 * Initialize execute_state object to a state where it's ready to
 * execute. Will validate the settings and serialize both settings and
 * the job_list into the result directory, overwriting old files if
 * settings set to do so.
 */
bool initialize_execute_state(struct execute_state *state,
			      struct settings *settings,
			      struct job_list *job_list);

/*
 * Initialize execute_state object to a state where it's ready to
 * resume an already existing run. settings and job_list must have
 * been initialized with init_settings et al, and will be read from
 * the result directory pointed to by dirfd.
 */
bool initialize_execute_state_from_resume(int dirfd,
					  struct execute_state *state,
					  struct settings *settings,
					  struct job_list *job_list);

bool execute(struct execute_state *state,
	     struct settings *settings,
	     struct job_list *job_list);


#endif
