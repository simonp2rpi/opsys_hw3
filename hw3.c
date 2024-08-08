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
void* handle_client(void* arg);
char* guessWord(const char* guess, const char* hidden_word, int* guesses_left);
int wordle_server(int argc, char **argv);

void sigusr1(int sig) {
    on = 0; 
    printf("MAIN: SIGUSR1 received; Wordle server shutting down...\n");
}

void* handle_client(void* arg) {
    int csocket = *(int*)arg;
    free(arg);

    pthread_t thread_id = pthread_self();

    pthread_mutex_lock(&lock);
    const char* hidden_word = *(words+(rand() % (total_guesses + 1)));
    pthread_mutex_unlock(&lock);

    int guesses = 6;
    char *guess = calloc(6,sizeof(char));
    while (guesses > 0) {
        printf("THREAD %lu: waiting for guess\n", (unsigned long)thread_id);
        int bytes_received = recv(csocket, guess, 5, 0);
        if (bytes_received <= 0) {
            printf("THREAD %lu: client gave up; closing TCP connection...\n", (unsigned long)thread_id);
            pthread_mutex_lock(&lock);
            pthread_mutex_unlock(&lock);
            total_losses++;
            close(csocket);
            pthread_exit(NULL);
        }

        int valid = 0;
        for (char** word = words; *word; word++) {
            if (!strcmp(guess, *word)) {
                valid = 1;
                break;
            }
        }
        printf("THREAD %lu: rcvd guess: %s\n", (unsigned long)thread_id, guess);
        if(!valid){
            printf("THREAD %lu: invalid guess; sending reply: ????? (%d guesses left)\n", (unsigned long)thread_id, guesses);
        }
        
        pthread_mutex_lock(&lock);
        pthread_mutex_unlock(&lock);
        total_guesses++;

        //only if valid guess
        if(valid){
            printf("THREAD %lu: sending reply: ", (unsigned long)thread_id);
        }

        char* wordle = guessWord(guess, hidden_word, &guesses);
        if (wordle == NULL) {
            send(csocket, "N?????\0", 8, 0);
        } else {
            send(csocket, wordle, 8, 0);
            free(wordle);
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

    close(csocket);
    pthread_exit(NULL);
}

char* guessWord(const char* guess, const char* hidden_word, int* guesses_left) {
    char* wordle = calloc(8, sizeof(char)); 
    if (!wordle) {
        perror("ERROR: calloc() failed");
        return NULL;
    }

    int valid = 0;
    for (char** word = words; *word; word++) {
        if (!strcmp(guess, *word)) {
            valid = 1;
            break;
        }
    }

    if (!valid) {
        strcpy(wordle, "N?????");
        return wordle; 
    }

    *(wordle+0) = 'Y';
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

    sprintf(wordle + 1, "%02d%s", *guesses_left, result);
    printf("%s  (%d guesses left)\n", result, *guesses_left);
    return wordle;
}

int wordle_server(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "ERROR: Invalid argument(s)\n");
        fprintf(stderr, "USAGE: hw3.out <listener-port> <seed> <dictionary-filename> <num-words>\n");
        return EXIT_FAILURE;
    }

    int seed = atoi(*(argv+2));
    int numWords = atoi(*(argv+4));

    srand(seed);
   //!!!
    FILE *file = fopen(*(argv+3), "r");
    if (!file) {
        perror("ERROR: failure opening dictionary file");
        exit(EXIT_FAILURE);
    }

    words = calloc(numWords + 1, sizeof(char*));
    if (!words) {
        perror("ERROR: calloc() failed");
        exit(EXIT_FAILURE);
    }

    char *word = calloc(6,sizeof(char));
    int i = 0;
    while (fscanf(file, "%5s", word) == 1 && i < numWords) {
        *(words+i) = calloc(strlen(word) + 1, sizeof(char));
        if (!*(words+i)) {
            perror("ERROR: calloc() failed");
            exit(EXIT_FAILURE);
        }

        for (int j = 0; *(word+j); j++) {
            *(word+j) = tolower(*(word+j));
        }
        strcpy(*(words+i), word);
        i++;
    }

    *(words+i) = NULL; 
    fclose(file);
    printf("MAIN: opened %s (%d words)\n", *(argv+3), i);
    if (pthread_mutex_init(&lock, NULL) != 0) {
        perror("ERROR: pthread_mutex_init() failed");
        return EXIT_FAILURE;
    }

    struct sigaction s;
    s.sa_handler = sigusr1;
    sigemptyset(&s.sa_mask);
    s.sa_flags = 0;
    sigaction(SIGUSR1, &s, NULL);

    int ssocket = socket(AF_INET, SOCK_STREAM, 0);
    if (ssocket < 0) {
        perror("ERROR: socket() failed");
        return EXIT_FAILURE;
    }

    struct sockaddr_in sAddress;
    memset(&sAddress, 0, sizeof(sAddress));
    sAddress.sin_family = AF_INET;
    sAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    sAddress.sin_port = htons(atoi(*(argv+1)));

    if (bind(ssocket, (struct sockaddr*)&sAddress, sizeof(sAddress)) < 0) {
        perror("ERROR: bind() failed");
        close(ssocket);
        return EXIT_FAILURE;
    }

    if (listen(ssocket, 5) < 0) {
        perror("ERROR: listen() failed");
        close(ssocket);
        return EXIT_FAILURE;
    }

    printf("MAIN: Wordle server listening on port {%d}\n",atoi(*(argv+1)));
    printf("MAIN: seeded pseudo-random number generator with %d\n", atoi(*(argv+2)));

    while (on) {
        struct sockaddr_in cAddress;
        socklen_t client_len = sizeof(cAddress);
        int* csocket = malloc(sizeof(int));
        if (!csocket) {
            perror("ERROR: malloc() failed");
            continue;
        }

        *csocket = accept(ssocket, (struct sockaddr*)&cAddress, &client_len);
        if (*csocket < 0) {
            perror("ERROR: accept() failed");
            free(csocket);
            continue;
        }

        printf("MAIN: rcvd incoming connection request\n");

        pthread_t thread;
        pthread_create(&thread, NULL, handle_client, csocket);
        pthread_detach(thread);
    }

    close(ssocket);
    pthread_mutex_destroy(&lock);
    for (int i = 0; *(words+i); i++) {
        free(*(words+i));
    }
    free(words);

    printf("MAIN: Wordle server shut down successfully\n");
    return EXIT_SUCCESS;
}