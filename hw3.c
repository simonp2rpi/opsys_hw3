#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>
#include <ctype.h>

extern int total_guesses;
extern int total_wins;
extern int total_losses;
extern char **words;

pthread_mutex_t lock;
int on = 1;

void sigusr1(int sig);
void load_dictionary(const char* filename, int num_words);
void* handle_client(void* arg);
char* guessWord(const char* guess, const char* hidden_word, int* guesses_left);
int wordle_server(int argc, char **argv);

void sigusr1(int sig) {
    on = 0; 
    printf("MAIN: SIGUSR1 received; Wordle server shutting down...\n");
}

void load_dictionary(const char* filename, int num_words) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("ERROR: failure opening dictionary file");
        exit(EXIT_FAILURE);
    }

    words = calloc(num_words + 1, sizeof(char*));
    if (!words) {
        perror("ERROR: calloc() failed");
        exit(EXIT_FAILURE);
    }

    char *word = calloc(6,sizeof(char));
    int index = 0;
    while (fscanf(file, "%5s", word) == 1 && index < num_words) {
        *(words+index) = calloc(strlen(word) + 1, sizeof(char));
        if (!*(words+index)) {
            perror("ERROR: calloc() failed");
            exit(EXIT_FAILURE);
        }

        for (int i = 0; *(word+i); i++) {
            *(word+i) = tolower(*(word+i));
        }
        strcpy(*(words+index), word);
        index++;
    }

    *(words+index) = NULL; 
    fclose(file);
    printf("MAIN: opened %s (%d words)\n", filename, index);
}
void* handle_client(void* arg) {
    int client_socket = *(int*)arg;
    free(arg);

    pthread_t thread_id = pthread_self();
    printf("THREAD %lu: waiting for guess\n", (unsigned long)thread_id);

    pthread_mutex_lock(&lock);
    const char* hidden_word = *(words+(rand() % (total_guesses + 1)));
    pthread_mutex_unlock(&lock);

    int guesses = 6;
    char *guess = calloc(6,sizeof(char));
    while (guesses > 0) {
        int bytes_received = recv(client_socket, guess, 5, 0);
        if (bytes_received <= 0) {
            printf("THREAD %lu: client gave up; closing TCP connection...\n", (unsigned long)thread_id);
            pthread_mutex_lock(&lock);
            total_losses++;
            pthread_mutex_unlock(&lock);
            close(client_socket);
            pthread_exit(NULL);
        }

        *(guess+5) = '\0'; 
        printf("THREAD %lu: received guess: %s\n", (unsigned long)thread_id, guess);

        pthread_mutex_lock(&lock);
        total_guesses++;
        pthread_mutex_unlock(&lock);

        char* reply = guessWord(guess, hidden_word, &guesses);
        if (reply == NULL) {
            char * sendWrong = calloc(9, sizeof(char));
            sprintf(sendInval, "N0%d?????", guesses);
            send(client_socket, sendInval, 8, 0);
        } else {
            send(client_socket, reply, 8, 0);
            free(reply);

            // Check if game is over
            if (strcmp(guess, hidden_word) == 0) {
                printf("THREAD %lu: game over; word was %s!\n", (unsigned long)thread_id, hidden_word);
                pthread_mutex_lock(&lock);
                total_wins++;
                pthread_mutex_unlock(&lock);
                break;
            } else if (guesses == 0) {
                printf("THREAD %lu: out of guesses; word was %s!\n", (unsigned long)thread_id, hidden_word);
                pthread_mutex_lock(&lock);
                total_losses++;
                pthread_mutex_unlock(&lock);
                break;
            }
        }
    }

    close(client_socket);
    pthread_exit(NULL);
}

char* guessWord(const char* guess, const char* hidden_word, int* guesses_left) {
    char* reply = calloc(8, sizeof(char)); 
    if (!reply) {
        perror("ERROR: calloc() failed");
        return NULL;
    }

    int valid = 0;
    for (char** word = words; *word; word++) {
        if (strcmp(guess, *word) == 0) {
            valid = 1;
            break;
        }
    }

    if (!valid) {
        strcpy(reply, "N?????");
        return reply; 
    }

    *(reply+0) = 'Y';
    (*guesses_left)--;

    char *result = calloc(6,sizeof(int)); 
    strcpy(result, "-----");
    int *ret = calloc(5,sizeof(int)); 

    for (int i = 0; i < 5; i++) {
        if (*(guess+i) == *(hidden_word+i)) {
            *(result+i) = toupper(*(guess+i));
            *(ret+i) = 1;
        }
    }

    for (int i = 0; i < 5; i++) {
        if (!*(ret+i)) {
            for (int j = 0; j < 5; j++) {
                if (!*(ret+j) && *(guess+i) == *(hidden_word+j)) {
                    *(result+i) = tolower(*(guess+i));
                    *(ret+j) = 1;
                    break;
                }
            }
        }
    }

    sprintf(reply + 1, "%02d%s", *guesses_left, result);
    return reply;
}

int wordle_server(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "ERROR: Invalid argument(s)\n");
        fprintf(stderr, "USAGE: hw3.out <listener-port> <seed> <dictionary-filename> <num-words>\n");
        return EXIT_FAILURE;
    }

    int port = atoi(*(argv+1));
    int seed = atoi(*(argv+2));
    const char* dictionary_file = *(argv+3);
    int num_words = atoi(*(argv+4));

    srand(seed);
    load_dictionary(dictionary_file, num_words);

    if (pthread_mutex_init(&lock, NULL) != 0) {
        perror("ERROR: pthread_mutex_init() failed");
        return EXIT_FAILURE;
    }

    struct sigaction sa;
    sa.sa_handler = sigusr1;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGUSR1, &sa, NULL);

    // Create a socket
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("ERROR: socket() failed");
        return EXIT_FAILURE;
    }

    // Bind the socket to the port
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);

    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("ERROR: bind() failed");
        close(server_socket);
        return EXIT_FAILURE;
    }

    // Start listening
    if (listen(server_socket, 5) < 0) {
        perror("ERROR: listen() failed");
        close(server_socket);
        return EXIT_FAILURE;
    }

    printf("MAIN: Wordle server listening on port %d\n", port);

    while (on) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int* client_socket = malloc(sizeof(int));
        if (!client_socket) {
            perror("ERROR: malloc() failed");
            continue;
        }

        *client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        if (*client_socket < 0) {
            perror("ERROR: accept() failed");
            free(client_socket);
            continue;
        }

        pthread_t thread;
        pthread_create(&thread, NULL, handle_client, client_socket);
        pthread_detach(thread); // No need to join
    }

    // Cleanup
    close(server_socket);
    pthread_mutex_destroy(&lock);
    for (int i = 0; *(words+i); i++) {
        free(*(words+i));
    }
    free(words);

    printf("MAIN: Wordle server shut down successfully\n");
    return EXIT_SUCCESS;
}