# REEFS - Rather Eerie Example of FTP Server

A simple implementation of good ol' FTP protocol on the server side.

Designed to as much POSIX-complaint and cross-platform as possible.

## Features

* Login as anonymous or with predefined credentials
* Walking through directories
* Uploading & downloading files (PASV mode only)

## Usage

Clone, compile and run:

    $ git clone git://github.com/Xion/reefs.git && cd reefs
    $ make
    $ ./bin/reefs

Server will read its configuration from _config_ and list of users from _users_.
Refer to those files for configuration options.
