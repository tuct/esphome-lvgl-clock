# BUILD_FROM comes from build.yaml, which is where the per-architecture base
# images live. The default here is only for a plain `docker build` outside the
# Supervisor - on real hardware it is always overridden.
ARG BUILD_FROM=ghcr.io/home-assistant/amd64-base:3.24
FROM $BUILD_FROM

# Python only - the server is stdlib, and its whole job is to hand the browser
# some JavaScript and forward one service call. ESPHome is deliberately NOT
# installed: this add-on does not compile and does not flash. A pattern is
# data, not firmware, so writing one to the wall needs neither.
RUN apk add --no-cache python3

# app/www is STAGED by stage.sh and must exist before this runs - Docker cannot
# reach outside its build context, so the sandbox and the cards are copied in.
COPY app /opt/dcc24
COPY run.sh /run.sh
RUN chmod a+x /run.sh

CMD [ "/run.sh" ]
