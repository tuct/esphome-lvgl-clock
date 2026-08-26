ARG BUILD_FROM=ghcr.io/home-assistant/amd64-base:latest
FROM $BUILD_FROM

# Python only - the server is stdlib, and its whole job is to hand the browser
# some JavaScript and forward one service call. ESPHome is deliberately NOT
# installed: this add-on does not compile and does not flash. A pattern is
# data, not firmware, so writing one to the wall needs neither.
RUN apk add --no-cache python3

COPY app /opt/dcc24
COPY run.sh /run.sh
RUN chmod a+x /run.sh

CMD [ "/run.sh" ]
