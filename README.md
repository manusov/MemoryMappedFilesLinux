# MemoryMappedFilesLinux

Memory-mapped files benchmark for Linux 32/64.
Privileged user required (sudo).


run:

"sudo ./mapfile [options]"

options list:

path=file path  , target file path and name, default myfile.bin in the current directory

size=block size , default 1GB, default units=bytes (possible K/M/G), examples: 10240, 16K, 20M, 1G

wsync=0|1       , don't synchronize write(0), synchronize(1), default=synchronize(1)

wdelay=value    , delay from start to write, milliseconds

rdelay=value    , delay from write end to read, milliseconds

repeats=value   , number of times to repeat test, for measurement


run examples (default and custom):

"sudo ./mapfile"
"sudo ./mapfile path=myfile.bin size=100K wsync=0 wdelay=3 rdelay=5 repeats=2"

File name is myfile.bin

size is 100 kilobytes

wait write synchronization not used

delay before write is 3 milliseconds

delay before read is 5 milliseconds

2 measurement repeats








