/*******************************************************************************
#                                                                              #
#      MJPG-streamer allows to stream JPG frames from an input-plugin          #
#      to several output plugins                                               #
#                                                                              #
#      Copyright (C) 2007 Tom Stöveken                                         #
#                                                                              #
# This program is free software; you can redistribute it and/or modify         #
# it under the terms of the GNU General Public License as published by         #
# the Free Software Foundation; version 2 of the License.                      #
#                                                                              #
# This program is distributed in the hope that it will be useful,              #
# but WITHOUT ANY WARRANTY; without even the implied warranty of               #
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                #
# GNU General Public License for more details.                                 #
#                                                                              #
# You should have received a copy of the GNU General Public License            #
# along with this program; if not, write to the Free Software                  #
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA    #
#                                                                              #
*******************************************************************************/
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <pthread.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/inotify.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "../../mjpg_streamer.h"
#include "../../utils.h"

#define INPUT_PLUGIN_NAME "fifo input plugin"
/*
typedef enum _read_mode {
    NewFilesOnly,
    ExistingFiles
} read_mode;
*/

/* private functions and variables to this plugin */
static pthread_t   worker;
static globals     *pglobal;

void *worker_thread(void *);
void worker_cleanup(void *);
void help(void);

static int delay = 20;

static const char* fifoname_default = "/run/mjpg-encoder.out.fifo";
static const char* frame_delim_default = "c3f97bee765fd86b209951ead9f8a583";

static char *fifoname = NULL;

static char     *fifo_buffer = NULL;
static const int fifo_buffer_size = 64000;

static char *frame_delim = NULL;
static int   frame_delim_size;

static int plugin_number;
//static read_mode mode = NewFilesOnly;

void init_defaults() {
    frame_delim_size = strlen(frame_delim_default);
    frame_delim = (char*)frame_delim_default;
    fifoname = (char*)fifoname_default;
}

/*** plugin interface functions ***/
int input_init(input_parameter *param, int id)
{
    int i;
    
    init_defaults();
        
    plugin_number = id;

    param->argv[0] = INPUT_PLUGIN_NAME;

    /* show all parameters for DBG purposes */
    for(i = 0; i < param->argc; i++) {
        DBG("argv[%d]=%s\n", i, param->argv[i]);
    }

    reset_getopt();
    while(1) {
        int option_index = 0, c = 0;
        static struct option long_options[] = {
            {"h", no_argument, 0, 0},
            {"help", no_argument, 0, 0},
            {"d", required_argument, 0, 0},
            {"delay", required_argument, 0, 0},
            {"n", required_argument, 0, 0},
            {"name", required_argument, 0, 0},
            {"dl", required_argument, 0, 0},
            {"delim", required_argument, 0, 0},
            {0, 0, 0, 0}
        };

        c = getopt_long_only(param->argc, param->argv, "", long_options, &option_index);

        /* no more options to parse */
        if(c == -1) break;

        /* unrecognized option */
        if(c == '?') {
            help();
            return 1;
        }

        switch(option_index) {
            /* h, help */
        case 0:
        case 1:
            DBG("case 0,1\n");
            help();
            return 1;
            break;

            /* d, delay */
        case 2:
        case 3:
            DBG("case 2,3\n");
            delay = atoi(optarg);
            break;

            /* n, name */
        case 4:
        case 5:
            DBG("case 4,5\n");
            fifoname = strdup(optarg);
            break;

            /* dl, delim */
        case 6:
        case 7:
            DBG("case 6,7\n");
            frame_delim = strdup(optarg);
            frame_delim_size = strlen(frame_delim);            
            break;
        default:
            DBG("default case\n");
            help();
            return 1;
        }
    }

    pglobal = param->global;

    //TODO: WARNING ABOUT DEFAULTS
    
    IPRINT("forced delay......: %i\n", delay);
    IPRINT("frame delimeter...: %s\n", frame_delim);
    IPRINT("fifoname is.......: %s\n", fifoname);

    param->global->in[id].name = malloc((strlen(INPUT_PLUGIN_NAME) + 1) * sizeof(char));
    sprintf(param->global->in[id].name, INPUT_PLUGIN_NAME);

    return 0;
}

int input_stop(int id)
{
    DBG("will cancel input thread\n");
    pthread_cancel(worker);
    return 0;
}

int input_run(int id)
{
    pglobal->in[id].buf = NULL;

    if(pthread_create(&worker, 0, worker_thread, NULL) != 0) {
        free(pglobal->in[id].buf);
        fprintf(stderr, "could not start worker thread\n");
        exit(EXIT_FAILURE);
    }

    pthread_detach(worker);

    return 0;
}

/*** private functions for this plugin below ***/
void help(void)
{
    fprintf(stderr, " ---------------------------------------------------------------\n" \
                    " Help for input plugin..: "INPUT_PLUGIN_NAME"\n" \
                    " ---------------------------------------------------------------\n" \
                    " The following parameters can be passed to this plugin:\n\n" \
                    " [-d | --delay ]........: delay to pause between frames\n" \
                    " [-n | --name ].........: full path to fifo\n" \
                    " [-e | --existing ].....: serve the existing *.jpg files from the specified directory\n" \
                    " ---------------------------------------------------------------\n");
}

#define fifo_buffer_available() (fifo_buffer_size - fifo_buffer_used)
/* the single writer thread */
void *worker_thread(void *arg)
{
    int frame_size;
    int fifo_fd;
    struct timeval timestamp;

//label
    fifo_fd = open(fifoname, O_RDONLY);
    if(fifo_fd == -1) {
        perror("Could not open fifo for reading");
        return NULL;
    }

    /* set cleanup handler to cleanup allocated ressources */
    pthread_cleanup_push(worker_cleanup, NULL);

    fifo_buffer  = (char*)malloc(fifo_buffer_size);
    size_t fifo_buffer_used = 0;
    char *head;
    char* frame_delim1;
    
    pglobal->in[plugin_number].buf = malloc(fifo_buffer_size); 
    while(!pglobal->stop) {
      frame_delim1 = NULL;
      
      int read_res = read(fifo_fd, fifo_buffer + fifo_buffer_used, fifo_buffer_available());
      if (read_res <= 0)
      {
        if (read_res == 0)
          fprintf(stderr, "read(%d, %zu) eof\n", fifo_fd, fifo_buffer_available());
        else {
          fprintf(stderr, "read(%d, %zu) failed: %d\n", fifo_fd, fifo_buffer_available(), read_res);
          perror("");
          
          //reopen
          if (fifo_fd != -1 && close(fifo_fd) != 0) {
            int res = errno;
            fprintf(stderr, "close() failed: %d\n", res);
            fifo_fd = -1;
            return NULL;
          }

          fifo_fd = open(fifoname, O_RDONLY);
          if (fifo_fd < 0) {
            int res = errno;
            fprintf(stderr, "open(%s) failed: %d\n", fifoname, res);
            fifo_fd = -1;
            return NULL;
          }
          
          fifo_buffer_used = 0;    
          fprintf(stderr,"reopened\n");
        }
        
        sleep(1);
        continue;
      }

      fifo_buffer_used += read_res;
      head = fifo_buffer;
      int frame_cnt = 0;
      while(NULL != (frame_delim1 = (char*)memmem(head, fifo_buffer_used - (head - fifo_buffer), frame_delim, frame_delim_size))) {
        frame_size = frame_delim1 - head;
        
        if(frame_size != 0) {

          /* copy frame from file to global buffer */
          pthread_mutex_lock(&pglobal->in[plugin_number].db);

          pglobal->in[plugin_number].size = frame_size;
          //memcpy(pglobal->in[plugin_number].buf, head, frame_size);
          pglobal->in[plugin_number].buf =  head;
          frame_cnt++;
          gettimeofday(&timestamp, NULL);
          pglobal->in[plugin_number].timestamp = timestamp;
          DBG("new frame copied (size: %d)\n", pglobal->in[plugin_number].size);
          /* signal fresh_frame */
          pthread_cond_broadcast(&pglobal->in[plugin_number].db_update);
          pthread_mutex_unlock(&pglobal->in[plugin_number].db);
         
        } else {
           perror("empty frame");
        }
        
        head = frame_delim1 + frame_delim_size;

        if(delay != 0)
            usleep(1000 * delay);

      }

      fifo_buffer_used -= (head - fifo_buffer);
      memmove(fifo_buffer, head, fifo_buffer_used);
    }

//thread_quit:
    free(pglobal->in[plugin_number].buf);
    DBG("leaving input thread, calling cleanup function now\n");
    /* call cleanup handler, signal with the parameter */
    pthread_cleanup_pop(1);

    return NULL;
}
#undef fifo_buffer_available

void worker_cleanup(void *arg)
{
    static unsigned char first_run = 1;

    if(!first_run) {
        DBG("already cleaned up ressources\n");
        return;
    }

    first_run = 0;
    DBG("cleaning up ressources allocated by input thread\n");

    if(pglobal->in[plugin_number].buf != NULL) free(pglobal->in[plugin_number].buf);
 
}
