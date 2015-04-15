#!/bin/sh

### BEGIN INIT INFO
# Provides:          app
# Required-Start:    
# Required-Stop:     
# Should-Start:      
# Default-Start:     2 3 4 5
# Default-Stop:      0 1 6
# Short-Description: start App MJPG_STREAMER
### END INIT INFO

MJPG_STREAMER_NAME=mjpg_streamer
MJPG_STREAMER_PATH=/usr/bin
MJPG_STREAMER_PRIORITY=-1
 
MJPG_STREAMER_PIDDIR=/var/run/
MJPG_STREAMER_PID=$PIDDIR/$NAME.pid

. /etc/init.d/functions

test -x $MJPG_STREAMER_PATH/$NAME || exit 0


enviroment () {
        export LD_LIBRARY_PATH=$MJPG_STREAMER_PATH:$LD_LIBRARY_PATH
        cd $MJPG_STREAMER_PATH

}

do_start() {
  enviroment
  start-stop-daemon -Svbx $MJPG_STREAMER_NAME -- -i "input_fifo.so" -o "output_http.so -w /www"
  sleep 1
  status $MJPG_STREAMER_NAME
  exit $?
}

do_stop() {
  start-stop-daemon -Kvx $MJPG_STREAMER_NAME
}

case $1 in 
        start)
                echo -n "Starting  $MJPG_STREAMER_NAME daemon : "
                do_start
                ;;
        stop)
                echo -n "Stopping $MJPG_STREAMER_NAME daemon: "
                do_stop
                ;;
        restart|force-reload)
                echo -n "Restarting $MJPG_STREAMER_NAME daemon: "
                do_stop
                do_start
                ;;
        status)
                enviroment 
                status $MJPG_STREAMER_NAME
                exit $?
                ;;
        *)
                echo "Usage: $0 {start|stop|force-reload|restart|status}"
                exit 1
        ;;
esac
exit 0

