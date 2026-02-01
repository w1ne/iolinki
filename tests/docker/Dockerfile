FROM zephyrprojectrtos/zephyr-build:latest

# Set up workspace
WORKDIR /workdir
RUN west init .
RUN west update --narrow

# Copy our project source into the modules area
# We mount the actual source at runtime, but this is the structure
# /workdir/modules/lib/iolinki

# Environment variables
ENV ZEPHYR_BASE=/workdir/zephyr
