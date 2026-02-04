# iolinki Full CI Environment (Zephyr + Linux + Quality)
FROM zephyrprojectrtos/zephyr-build:latest

# Switch to root to install quality tools
USER root

# Install static analysis and formatting tools
RUN apt-get update && apt-get install -y \
    cppcheck \
    clang-format \
    doxygen \
    graphviz \
    && rm -rf /var/lib/apt/lists/*

# Switch back to user 'user' (default in zephyr images)
USER user

# Create workspace
WORKDIR /workdir/iolinki

# Copy project files (Owner must be user)
COPY --chown=user:user . .

# Initialize Zephyr Workspace (Caches Zephyr OS in the image)
# We initialize the workspace in /workdir, linking this repo as the manifest module
RUN west init -l . && \
    west update && \
    west zephyr-export

# Install python dependencies for tests if defined
# RUN pip3 install -r requirements.txt (if we had one)

# Make scripts executable
RUN chmod +x test_all.sh check_quality.sh tests/test_zephyr.sh tools/zephyr_wrapper.sh

# Default command: Run Quality Checks then All Tests (including Zephyr)
CMD ["/bin/bash", "-c", "./check_quality.sh && ./test_all.sh"]
