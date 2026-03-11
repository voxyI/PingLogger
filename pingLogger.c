#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>

#define ICMP_PAYLOAD_LENGTH (64 - sizeof(struct icmphdr))
#define TIMEOUT 2500
#define NUM_PINGS 10
#define FREQUENCY 30
#define IP_HEADER_LENGTH 20

char sig_flag = 1;

void sig_handler() {
	sig_flag = 0;
}

void clear_data(void * data, int size) {
    memset(data, 0, size);
}

struct ping_pkt {
    struct icmphdr hdr;
    char payload[ICMP_PAYLOAD_LENGTH];
};

unsigned short calc_checksum(const struct ping_pkt * packet) {
    const unsigned short * view = (const unsigned short *)packet;
    size_t size = sizeof(struct ping_pkt);

    unsigned int sum = 0;
    for (; size > 1; size -= 2)
    {
        sum += *view++;
    }
    if (size == 1)
    {
        sum += *(unsigned char *)view;
    }
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    return ~sum;
}

const char *dns_lookup(const char *address, struct sockaddr_in *sock_addr) {
    static char buffer[1024];

    clear_data(&buffer, sizeof(buffer));
    const uint16_t port = 0;
    struct hostent *host_entity = gethostbyname(address);
    if (host_entity == NULL) {
        printf("gethostbyname for '%s' failed.\n", address);
        return NULL;
    }
    const char *name = inet_ntoa(*(struct in_addr *)host_entity->h_addr_list[0]);
    strncpy(buffer, name, sizeof(buffer));
    if (sock_addr != NULL) {
        sock_addr->sin_family = host_entity->h_addrtype;
        sock_addr->sin_port = htons(port);
        sock_addr->sin_addr.s_addr = *(long *)host_entity->h_addr_list[0];
    }
    return &buffer[0];
}

int set_receive_timeout(int socket_fd, int timeout_ms) {
    int seconds = timeout_ms / 1000;
    int useconds = (timeout_ms - (seconds * 1000)) * 1000;
    struct timeval tv_out;
    clear_data(&tv_out, sizeof(tv_out));
    tv_out.tv_sec = seconds;
    tv_out.tv_usec = useconds;
    return setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &tv_out, sizeof(tv_out));
}

void init_packet(struct ping_pkt *icmp_packet) {
    clear_data(icmp_packet, sizeof(*icmp_packet));
    icmp_packet->hdr.type = ICMP_ECHO;
    icmp_packet->hdr.un.echo.id = getpid();
    icmp_packet->hdr.un.echo.sequence = 0;

    for (size_t i = 0; i < ICMP_PAYLOAD_LENGTH; ++i)
    {
        icmp_packet->payload[i] = (char)('0' + i);
    }
    icmp_packet->hdr.checksum = calc_checksum(icmp_packet);
}

bool verify_reply(const struct ping_pkt *sent, const struct ping_pkt *received, int expected_id) {
    if (received->hdr.type != ICMP_ECHOREPLY) {
        return false;
    }
    if (received->hdr.code != 0) {
        return false;
    }
    if (received->hdr.un.echo.id != expected_id) {
        return false;
    }
    if (memcmp(&sent->payload[0], &received->payload[0], ICMP_PAYLOAD_LENGTH) != 0) {
        return false;
    }
    return true;
}

double calculate_time(const struct timespec *start_time, const struct timespec *end_time) {
    double ns = (end_time->tv_nsec - start_time->tv_nsec) / 1000000.0;
    double ms = (end_time->tv_sec - start_time->tv_sec) * 1000.0;
    return ms + ns;
}

int icmp_ping(const char *address, int timeout_ms, double *duration_ms) {
    struct sockaddr_in sock_addr;
    clear_data(&sock_addr, sizeof(sock_addr));
    const char * name = dns_lookup(address, &sock_addr);
    if (name == NULL) {
        return -1;
    }
    int socket_fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (socket_fd < 0) {
        printf("descriptor for icmp_socket to '%s' could not be created. (are you root?)\n", address);
        return -1;
    }

    int ttl = 64;
    if (setsockopt(socket_fd, SOL_IP, IP_TTL, &ttl, sizeof(ttl)) != 0) {
        printf("ttl for icmp_socket to '%s' could not be set.\n", address);
        close(socket_fd);
        return -1;
    }
    if (set_receive_timeout(socket_fd, timeout_ms) != 0) {
        printf("timeout for icmp_socket to '%s' could not be set.\n", address);
        close(socket_fd);
        return -1;
    }

    const uint16_t my_icmp_id = getpid();
    const int raw_icmp_response_length = IP_HEADER_LENGTH + sizeof(struct ping_pkt);

    struct ping_pkt packet;
    init_packet(&packet);

    struct timespec start_timestamp;
    struct timespec stop_timestamp;
    clock_gettime(CLOCK_MONOTONIC, &start_timestamp);

    sendto(socket_fd, &packet, sizeof(packet), 0, (struct sockaddr *) &sock_addr, sizeof(sock_addr));

    bool done = false;
    while (!done) {
        char buffer[1024];
        int data_received = recvfrom(socket_fd, &buffer[0], raw_icmp_response_length, 0, NULL, NULL);

        clock_gettime(CLOCK_MONOTONIC, &stop_timestamp);
        *duration_ms = calculate_time(&start_timestamp, &stop_timestamp);
        if (*duration_ms > timeout_ms)
        {
            done = true;
        }
        if (data_received == raw_icmp_response_length)
        {
            const struct ping_pkt * data = (const struct ping_pkt *)&buffer[IP_HEADER_LENGTH];
            if (verify_reply(&packet, data, my_icmp_id))
            {
                close(socket_fd);
                return 1;
            }
            printf("  warning unrelated message received of %d bytes with id %d.\n", data_received, data->hdr.un.echo.id);
            continue;
        }
        if (data_received > 0)
        {
            printf("  warning unrelated message received of %d bytes.\n", data_received);
        }
    }
    close(socket_fd);
    return -2;
}

double get_avg_ping(const char *host) {
	double sum, duration = 0.0;

	for(int i = 0; i < NUM_PINGS; i++){
		int result = icmp_ping(host, TIMEOUT, &duration);
		if(result == -2) {
			//ping timeout
			return -1.0;
		} else {
			sum += duration;
		}
	}

	return (sum / ((double) NUM_PINGS));
}

void format_timestamp(char *time) {
    int i = 0;
    while(time[i] != '\0')
        i++;

    i--;
    time[i] = '\0';
}

int main(int argc, char **argv) {
	if(argc < 3) {
		printf("usage: %s <host> <log_file>", argv[0]);
		return -1;
	}

	const char *hostname = argv[1];
	const char *addr = dns_lookup(hostname, NULL);
	const int timeout_ms = 2500;

	FILE *output;
	output = fopen(argv[2], "w");
	if(output == NULL){
		printf("File IO error\n");
		return -1;
	}

    time_t stime = time(NULL);
    char *stime_string = ctime(&stime);
    printf("Start time: %s\n", stime_string);

	signal(SIGINT, sig_handler);

	while(sig_flag) {
		double avg = get_avg_ping(hostname);
        time_t curtime = time(NULL);
        char *curtime_string = ctime(&curtime);
        format_timestamp(curtime_string);
		fprintf(output, "%s,%.1f\n", curtime_string, avg);
		printf("%s:\t%.1fms\n",curtime_string, avg);
		sleep(FREQUENCY);
	}

    time_t etime = time(NULL);
    char *etime_string = ctime(&etime);
    printf("\nEnd time: %s\n", etime_string);

    fclose(output);
    printf("\n%s closed\n", argv[2]);
    printf("graphing results...\n");

    char command[] = "python3 graph_results.py ";
    if(system(strcat(command, argv[2]))) {
        printf("\nError running graph_results.py on %s\n", argv[2]);
        return -1;
    }
    
	printf("\nExit successful\n");
}