# PingLogger
A simple tool used to track the latency of a network over time.

# Compiling and Running
Compile using `gcc pingLogger.c -o pingLogger`

Run using `sudo ./pingLogger <host> <output_file>` where `<host>` is the hostname of destination you would like to ping and `<output_file>` is a .csv file to store the ping stats.

The program will run forever until interrupted by the user using `CTRL+C`. After this interrupt, the program will stop executing ping request and graph the results of the test and display it to the user. A value of -1 will indicate that packets were losed during that time period.

By default, 10 ping request will be sent every 30 seconds and the average of those 10 request will be recorded. Both the number and frequency of those request can be changed by changing the `NUM_PINGS` and `PERIOD` macros.

Note: This must be run as root since it relies on the use of raw sockets, which are a previledged operation in Linux.
