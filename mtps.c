#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#define PORT_NUMBER 8080
#define MAX_CLIENTS 10
#define MAX_COMMAND_LEN 128
#define MAX_HISTORY_ENTRIES 100
// account structure
struct account {
char username[32];
char password[32];
double balance;
int sock_fd; // socket file descriptor of logged-in client
char transaction_history[MAX_HISTORY_ENTRIES][MAX_COMMAND_LEN];
int history_count;
};
struct account accounts[MAX_CLIENTS];
// Mutex for accessing shared resources
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
// authenticate function
int authenticate(char* username, char* password) {
for (int i = 0; i < MAX_CLIENTS; i++) {
if (strcmp(username, accounts[i].username) == 0 &&
strcmp(password, accounts[i].password) == 0) {
return i; // authentication successful
}
}
return -1; // authentication failed
}
// withdraw function
int withdraw(int account_index, double amount) {
if (accounts[account_index].balance < amount) {
return -1; // insufficient funds
}
accounts[account_index].balance -= amount;
return 0;
}
// deposit function
void deposit(int account_index, double amount) {
accounts[account_index].balance += amount;
}
// add transaction to history
void addTransactionToHistory(int account_index, const char* transaction) {
pthread_mutex_lock(&mutex);
if (accounts[account_index].history_count < MAX_HISTORY_ENTRIES) {
strcpy(accounts[account_index].transaction_history[accounts[account_index].history
_count],
transaction);
accounts[account_index].history_count++;
} else {
// Shift the history entries to make space for the new entry
for (int i = 0; i < MAX_HISTORY_ENTRIES - 1; i++) {
strcpy(accounts[account_index].transaction_history[i],
accounts[account_index].transaction_history[i + 1]);
}
strcpy(accounts[account_index].transaction_history[MAX_HISTORY_ENTRIES - 1],
transaction);
}
pthread_mutex_unlock(&mutex);
}
// handle client function
void* handle_client(void* arg) {
int sock_fd = *(int*)arg;
free(arg);
// read command from client
char command_buf[MAX_COMMAND_LEN];
int bytes_read = recv(sock_fd, command_buf, sizeof(command_buf) - 1, 0);
if (bytes_read < 0) {
perror("ERROR reading from socket");
close(sock_fd);
pthread_exit(NULL);
}
command_buf[bytes_read] = '\0';
// parse command
char* command = strtok(command_buf, " \r\n");
char* username = strtok(NULL, " \r\n");
char* password = strtok(NULL, " \r\n");
char* amount_str = strtok(NULL, " \r\n");
if (strcmp(command, "LOGIN") == 0) {
int account_index = authenticate(username, password);
if (account_index < 0) {
// authentication failed
char* message = "ERROR Authentication failed\n";
send(sock_fd, message, strlen(message), 0);
} else if (accounts[account_index].sock_fd >= 0) {
// account already logged in
char* message = "ERROR Account already logged in\n";
send(sock_fd, message, strlen(message), 0);
} else {
accounts[account_index].sock_fd = sock_fd;
char* message = "SUCCESS\n";
send(sock_fd, message, strlen(message), 0);
}
} else if (strcmp(command, "CREATE") == 0) {
int account_index = -1;
pthread_mutex_lock(&mutex);
for (int i = 0; i < MAX_CLIENTS; i++) {
if (strcmp(accounts[i].username, username) == 0) {
account_index = i;
break;
}
}
if (account_index >= 0) {
// account with the same username already exists
char* message = "ERROR Account already exists\n";
send(sock_fd, message, strlen(message), 0);
} else {
// find an available account slot
for (int i = 0; i < MAX_CLIENTS; i++) {
if (accounts[i].sock_fd == -1) {
account_index = i;
strcpy(accounts[i].username, username);
strcpy(accounts[i].password, password);
accounts[i].balance = 0.0;
accounts[i].history_count = 0;
break;
}
}
if (account_index < 0) {
// maximum number of clients reached
char* message = "ERROR Maximum number of accounts reached\n";
send(sock_fd, message, strlen(message), 0);
} else {
// account creation successful
char* message = "SUCCESS Account created\n";
send(sock_fd, message, strlen(message), 0);
}
}
pthread_mutex_unlock(&mutex);
} else if (strcmp(command, "BALANCE") == 0) {
// find account index based on socket fd
int account_index = -1;
for (int i = 0; i < MAX_CLIENTS; i++) {
if (accounts[i].sock_fd == sock_fd) {
account_index = i;
break;
}
}
if (account_index < 0) {
// account not found
char* message = "ERROR Account not found\n";
send(sock_fd, message, strlen(message), 0);
} else {
// send account balance to client
char balance_str[32];
snprintf(balance_str, sizeof(balance_str), "%.2f", accounts[account_index].balance);
char message[MAX_COMMAND_LEN];
snprintf(message, sizeof(message), "SUCCESS %s\n", balance_str);
send(sock_fd, message, strlen(message), 0);
}
} else if (strcmp(command, "WITHDRAW") == 0) {
// find account index based on socket fd
int account_index = -1;
for (int i = 0; i < MAX_CLIENTS; i++) {
if (accounts[i].sock_fd == sock_fd) {
account_index = i;
break;
}
}
if (account_index < 0) {
// account not found
char* message = "ERROR Account not found\n";
send(sock_fd, message, strlen(message), 0);
} else {
// convert amount to double
double amount = atof(amount_str);
// withdraw amount from account
int result = withdraw(account_index, amount);
if (result < 0) {
// insufficient funds
char* message = "ERROR Insufficient funds\n";
send(sock_fd, message, strlen(message), 0);
} else {
// update transaction history
char transaction[MAX_COMMAND_LEN];
snprintf(transaction, sizeof(transaction), "WITHDRAW %.2f", amount);
addTransactionToHistory(account_index, transaction);
// send success message to client
char* message = "SUCCESS Withdrawal successful\n";
send(sock_fd, message, strlen(message), 0);
}
}
} else if (strcmp(command, "DEPOSIT") == 0) {
// find account index based on socket fd
int account_index = -1;
for (int i = 0; i < MAX_CLIENTS; i++) {
if (accounts[i].sock_fd == sock_fd) {
account_index = i;
break;
}
}
if (account_index < 0) {
// account not found
char* message = "ERROR Account not found\n";
send(sock_fd, message, strlen(message), 0);
} else {
// convert amount to double
double amount = atof(amount_str);
// deposit amount into account
deposit(account_index, amount);
// update transaction history
char transaction[MAX_COMMAND_LEN];
snprintf(transaction, sizeof(transaction), "DEPOSIT %.2f", amount);
addTransactionToHistory(account_index, transaction);
// send success message to client
char* message = "SUCCESS Deposit successful\n";
send(sock_fd, message, strlen(message), 0);
}
} else if (strcmp(command, "HISTORY") == 0) {
// find account index based on socket fd
int account_index = -1;
for (int i = 0; i < MAX_CLIENTS; i++) {
if (accounts[i].sock_fd == sock_fd) {
account_index = i;
break;
}
}
if (account_index < 0) {
// account not found
char* message = "ERROR Account not found\n";
send(sock_fd, message, strlen(message), 0);
} else {
// send transaction history to client
char message[MAX_COMMAND_LEN * MAX_HISTORY_ENTRIES];
char temp[MAX_COMMAND_LEN];
strcpy(message, "SUCCESS Transaction History:\n");
for (int i = 0; i < accounts[account_index].history_count; i++) {
snprintf(temp, sizeof(temp), "%s\n",
accounts[account_index].transaction_history[i]);
strcat(message, temp);
}
send(sock_fd, message, strlen(message), 0);
}
} else {
char* message = "ERROR Invalid command\n";
send(sock_fd, message, strlen(message), 0);
}
// close socket and reset account sock_fd
close(sock_fd);
for (int i = 0; i < MAX_CLIENTS; i++) {
if (accounts[i].sock_fd == sock_fd) {
accounts[i].sock_fd = -1;
break;
}
}
pthread_exit(NULL);
}
int main() {
// initialize accounts
for (int i = 0; i < MAX_CLIENTS; i++) {
accounts[i].sock_fd = -1;
accounts[i].history_count = 0;
}
// create thread pool
pthread_t thread_pool[MAX_CLIENTS];
int thread_index = 0;
// create server socket
int server_sock_fd = socket(AF_INET, SOCK_STREAM, 0);
if (server_sock_fd < 0) {
perror("ERROR opening server socket");
exit(1);
}
// set server address
struct sockaddr_in server_address;
memset(&server_address, 0, sizeof(server_address));
server_address.sin_family = AF_INET;
server_address.sin_addr.s_addr = INADDR_ANY;
server_address.sin_port = htons(PORT_NUMBER);
// bind server address to socket
if (bind(server_sock_fd, (struct sockaddr*)&server_address, sizeof(server_address))
< 0) {
perror("ERROR binding server socket");
exit(1);
}
// listen for client connections
if (listen(server_sock_fd, MAX_CLIENTS) < 0) {
perror("ERROR listening for clients");
exit(1);
}
printf("Server listening on port %d...\n", PORT_NUMBER);
while (1) {
// accept client connection
struct sockaddr_in client_address;
socklen_t client_address_length = sizeof(client_address);
int client_sock_fd = accept(server_sock_fd, (struct sockaddr*)&client_address,
&client_address_length);
if (client_sock_fd < 0) {
perror("ERROR accepting client connection");
continue;
}
// create thread to handle client
pthread_t thread;
int* new_sock_fd = (int*)malloc(sizeof(int));
*new_sock_fd = client_sock_fd;
if (pthread_create(&thread, NULL, handle_client, (void*)new_sock_fd) != 0) {
perror("ERROR creating thread");
free(new_sock_fd);
close(client_sock_fd);
continue;
}
// add thread to thread pool
thread_pool[thread_index++] = thread;
if (thread_index >= MAX_CLIENTS) {
// join completed threads to free up resources
for (int i = 0; i < MAX_CLIENTS; i++) {
pthread_join(thread_pool[i], NULL);
}
thread_index = 0;
}
}
// close server socket
close(server_sock_fd);
return 0;
}
