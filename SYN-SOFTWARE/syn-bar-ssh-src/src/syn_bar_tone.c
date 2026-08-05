/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-BAR-TONE
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#define _POSIX_C_SOURCE 200809L

#include "syn_bar_tone.h"

#include <stdint.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SAMPLE_RATE 44100

void syn_bar_tone_play(double hz, double seconds) {
	pid_t pid = fork();
	if (pid < 0) {
		return;
	}
	if (pid == 0) {
		int pipefd[2];
		if (pipe(pipefd) != 0) {
			_exit(1);
		}
		pid_t player = fork();
		if (player < 0) {
			_exit(1);
		}
		if (player == 0) {
			dup2(pipefd[0], STDIN_FILENO);
			close(pipefd[0]);
			close(pipefd[1]);
			int devnull = open("/dev/null", O_WRONLY);
			if (devnull >= 0) {
				dup2(devnull, STDOUT_FILENO);
				dup2(devnull, STDERR_FILENO);
				close(devnull);
			}
			execlp("paplay", "paplay", "--raw",
				"--rate=44100", "--format=s16le", "--channels=1",
				(char *)NULL);
			_exit(127);
		}
		close(pipefd[0]);

		int total_samples = (int)(SAMPLE_RATE * seconds);
		int fade_samples = SAMPLE_RATE / 200; /* 5ms fade */
		for (int i = 0; i < total_samples; i++) {
			double t = (double)i / SAMPLE_RATE;
			double amp = 0.5;
			if (i < fade_samples) {
				amp *= (double)i / fade_samples;
			} else if (i > total_samples - fade_samples) {
				amp *= (double)(total_samples - i) / fade_samples;
			}
			int16_t sample = (int16_t)(amp * 32767.0 * sin(2.0 * M_PI * hz * t));
			if (write(pipefd[1], &sample, sizeof(sample)) != sizeof(sample)) {
				break;
			}
		}
		close(pipefd[1]);

		int status;
		waitpid(player, &status, 0);
		_exit(0);
	}
	/* Parent doesn't wait — the intermediate fork above does that, so
	 * the caller (a short poll-driven process) returns immediately. */
}
