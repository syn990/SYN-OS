#!/bin/zsh
# =============================================================================
#                                 SYN-OS .zshrc
#                      Zsh Configuration for SYN-OS Users
# -----------------------------------------------------------------------------
#   This file sets up the Zsh environment for users in SYN-OS, including 
#   Oh-My-Zsh configuration, plugins, and language settings.
#   Author: William Hayward-Holland (Syntax990)
#   License: MIT
# =============================================================================

# Define path to Oh-My-Zsh installation for the default user
DEFAULT_USER_990=$USER
export ZSH="/home/$DEFAULT_USER_990/.oh-my-zsh"

# Set Zsh theme (random for variety)
ZSH_THEME="random"

# Load plugins (keep it minimal to optimize startup time)
plugins=(git)

# Source Oh-My-Zsh configuration
source $ZSH/oh-my-zsh.sh

# Set language environment
export LANG=en_GB.UTF-8

# Define nano as the default text editor
export EDITOR='nano'
