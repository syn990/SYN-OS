#!/bin/zsh

# SYN-OS
# SYNTAX990
# WILLIAM HAYWARD-HOLLAND
# M.I.T LICENSE

# Path to your oh-my-zsh installation.

#not able to source this from syn-stage0.zsh
DEFAULT_USER_990=$USER

export ZSH="/home/$DEFAULT_USER_990/.oh-my-zsh"

# Theme for zsh
ZSH_THEME="random"

# Which plugins would you like to load?
# Standard plugins can be found in $ZSH/plugins/
# Custom plugins may be added to $ZSH_CUSTOM/plugins/
# Example format: plugins=(rails git textmate ruby lighthouse)
# Add wisely, as too many plugins slow down shell startup.
plugins=(git)

# Set zsh variable for some reason
source $ZSH/oh-my-zsh.sh

# You may need to manually set your language environment
export LANG=en_GB.UTF-8

# Setup nano as the default text editor
export EDITOR='nano'
