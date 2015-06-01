#!/bin/sh
echo "$3 ($4): $1C, $2%"

SR_TIME="class=\"time\">.*</span"
RP_TIME="class=\"time\"> $3 </span"
SR_TEMP="class=\"temp\">.*</span"
RP_TEMP="class=\"temp\"> $1\&deg;C $2% </span"

cat /www/light/index.html | sed -e s,"$SR_TIME","$RP_TIME", | sed -e s,"$SR_TEMP","$RP_TEMP",  > /tmp/index.html && cp /tmp/index.html /www/light/index.html

fswebcam -q -r 1280x720 --jpeg 85 /tmp/cam.jpg && mv /tmp/cam.jpg /www/light/img/cam.jpg
