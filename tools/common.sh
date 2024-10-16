# Here are the common variables & functions for all bash scripts.

# color codes
COLOR_RED='\033[0;31m'
COLOR_GREEN='\033[0;32m'
COLOR_YELLOW='\033[0;33m'
COLOR_BLUE='\033[0;34m'
COLOR_MAGENTA='\033[0;35m'
COLOR_END='\033[0m'

# Colorized echo functions.
echo_info() {
    echo -e "${COLOR_GREEN}$*${COLOR_END}"
}

echo_warning() {
    echo -e "${COLOR_YELLOW}$*${COLOR_END}"
}

echo_error() {
    echo -e "${COLOR_RED}$*${COLOR_END}"
}
