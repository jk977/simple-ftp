# MFTP

## About

A simple server/client program for transferring raw data files over TCP, with some extensions.

## Dependencies

* A version of `gcc` with C99 and POSIX 2008 compliance
* GNU `make`

## Compilation

To compile the executables, run `make` in the project root. The executables are placed in the `build` directory. The following environment variables are used by the Makefile:

* `DEBUG`: If defined, debugging symbols are included in the executables.
* `PORT` (optional): The port to have the server listen on. Defaults to `49999`.

## Running

After compiling, run `mftpserve` to start the server. To connect to the server, run `mktp SERVER`, where `SERVER` is the address of the server, either in dotted-decimal form or as a conventional host address. The following commands are available to the client:

* `cd PATH`: Change the client's working directory to `PATH`.
* `rcd PATH`: Change the server's working directory to `PATH`.
* `ls`: List the files in the client's working directory.
* `rls`: List the files in the server's working directory.
* `get PATH`: Store the server file at `PATH` in the client's working directory.
* `show PATH`: Display the contents of the server file at `PATH` to the client's stdout.
* `put PATH`: Store the client file at `PATH` in the server's working directory.
* `exit`: Exit the client.
