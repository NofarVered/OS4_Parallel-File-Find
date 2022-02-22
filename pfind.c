#define _BSD_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <limits.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct objPath
{
    char *path;
    struct objPath *next;
} objPath;

typedef struct QP
{
    objPath *head, *tail;
} QP;

typedef struct objThread
{
    long thread_id;
    pthread_cond_t *cv;
    struct objThread *next;
} objThread;

typedef struct QT
{
    objThread *head, *tail;
} QT;

const char *st;
long cntThreads;
pthread_mutex_t threadMut;
pthread_mutex_t path_queue_mutex;
pthread_mutex_t search_over_mutex;
pthread_cond_t *cv_arr;
pthread_cond_t all_threads_created;
pthread_cond_t search_over_cv;
atomic_long NumThreads = 0;
QT *sleeping_threads_queue;
QP *path_queue;
atomic_int finalCnt = 0;
atomic_int search_over = 0;

objThread *init_objThread(long thread_id, pthread_cond_t *cv)
{
    objThread *curr = malloc(sizeof(struct objThread));
    if (curr == NULL)
    {
        printf("ERROR");
        exit(1);
    }
    curr->cv = cv;
    curr->thread_id = thread_id;
    curr->next = NULL;
    return curr;
}

objPath *init_objPath(char *path, int is_main)
{
    objPath *cur = malloc(sizeof(struct objPath));
    if (cur == NULL)
    {
        printf("ERROR");
        if (is_main)
        {
            exit(1);
        }
        pthread_exit(NULL);
    }
    cur->path = path;
    cur->next = NULL;
    return cur;
}

void add_to_tail(QT *current_qu, objThread *node)
{
    if (current_qu->tail == NULL)
    {
        current_qu->head = current_qu->tail = node;
        return;
    }
    current_qu->tail->next = node;
    current_qu->tail = node;
    node->next = NULL;
}

objThread *insertQT(QT *current_qu)
{
    if (current_qu->head == NULL)
        return NULL;
    objThread *temp = current_qu->head;
    current_qu->head = current_qu->head->next;
    if (current_qu->head == NULL)
        current_qu->tail = NULL;
    temp->next = NULL;
    return temp;
}

void inserQP(QP *q, char *path, int is_main)
{
    char *node_path = malloc(sizeof(char) * PATH_MAX);
    if (node_path == NULL)
    {
        if (is_main)
        {
            exit(1);
        }
        pthread_exit(NULL);
    }
    strcpy(node_path, path);
    objPath *temp = init_objPath(node_path, is_main);
    if (q->tail == NULL)
    {
        q->head = q->tail = temp;
        return;
    }
    q->tail->next = temp;
    q->tail = temp;
    temp->next = NULL;
}

objPath *pathNode_deQueue(QP *q)
{
    if (q->head == NULL)
        return NULL;
    objPath *temp = q->head;
    q->head = q->head->next;
    if (q->head == NULL)
        q->tail = NULL;
    temp->next = NULL;
    return temp;
}

int len_QP(QP *q)
{
    int cnt = 0;
    objPath *head = q->head;
    while (head != NULL)
    {
        cnt++;
        head = head->next;
    }
    return cnt;
}

int len_QT(QT *q)
{
    int cnt = 0;
    objThread *head = q->head;
    while (head != NULL)
    {
        cnt++;
        head = head->next;
    }
    return cnt;
}

void free_QP(QP *q)
{
    objPath *head = q->head;
    objPath *temp = head;
    while (head != NULL)
    {
        head = head->next;
        free(temp->path);
        free(temp);
        temp = head;
    }
}

void free_QT(QT *q)
{
    objThread *head = q->head;
    objThread *temp = head;
    while (head != NULL)
    {
        head = head->next;
        free(temp);
        temp = head;
    }
}

int is_directory(const char *path)
{
    struct stat stat_buf;
    if (stat(path, &stat_buf) != 0)
        pthread_exit(NULL);
    return S_ISDIR(stat_buf.st_mode);
}

void searchD(long my_id,
             char *search_path)
{
    char *path = malloc(sizeof(char) * PATH_MAX);
    if (path == NULL)
    {
        printf("ERROR");
        pthread_exit(NULL);
    }
    char *display_path = malloc(sizeof(char) * PATH_MAX);
    if (display_path == NULL)
    {
        printf("ERROR");
        pthread_exit(NULL);
    }
    struct dirent *dp;
    DIR *dir;
    char *entry_name;
    char *found;
    objThread *nextST;

    dir = opendir(search_path);
    if (!dir)
    {
        printf("Directory %s: Permission denied.\n", search_path);
        return;
    }

    while ((dp = readdir(dir)) != NULL)
    {
        entry_name = dp->d_name;
        if (strcmp(entry_name, ".") != 0 && strcmp(entry_name, "..") != 0)
        {
            strcpy(path, search_path);
            strcat(path, "/");
            strcat(path, entry_name);

            if (is_directory(path))
            {
                pthread_mutex_lock(&path_queue_mutex);
                inserQP(path_queue, path, 0);
                pthread_mutex_unlock(&path_queue_mutex);
                pthread_mutex_lock(&threadMut);
                nextST = insertQT(sleeping_threads_queue);
                pthread_mutex_unlock(&threadMut);
                if (nextST != NULL)
                {
                    pthread_cond_signal(nextST->cv);
                }
            }
            else
            {
                found = strstr(entry_name, st);
                if (found)
                {
                    strcpy(display_path, search_path);
                    strcat(display_path, "/");
                    strcat(display_path, entry_name);
                    printf("%s\n", display_path);
                    finalCnt++;
                }
            }
        }
    }
    free(path);
    free(display_path);
    closedir(dir);
}

void *search_thread_func(void *t)
{
    long my_id = (long)t;
    int i;
    int pathQueueSize;
    objThread *my_node;
    objPath *search_path;
    pthread_mutex_lock(&threadMut);
    NumThreads++;
    pthread_cond_signal(&all_threads_created);
    pthread_mutex_unlock(&threadMut);
    pthread_mutex_lock(&threadMut);
    pthread_cond_wait(&cv_arr[my_id], &threadMut);
    pthread_mutex_unlock(&threadMut);
    pthread_mutex_lock(&search_over_mutex);
    if (search_over)
    {
        pthread_mutex_unlock(&search_over_mutex);
        pthread_exit(NULL);
    }
    pthread_mutex_unlock(&search_over_mutex);

    while (1)
    {
        pthread_mutex_lock(&threadMut);
        pathQueueSize = len_QP(path_queue);
        pthread_mutex_unlock(&threadMut);
        while (pathQueueSize > 0)
        {
            pthread_mutex_lock(&threadMut);
            search_path = pathNode_deQueue(path_queue);
            pthread_mutex_unlock(&threadMut);
            if (search_path == NULL)
            {
                break;
            }
            else
            {
                searchD(my_id, search_path->path);
                usleep(1);
            }
            pthread_mutex_lock(&threadMut);
            pathQueueSize = len_QP(path_queue);
            pthread_mutex_unlock(&threadMut);
        }
        pthread_mutex_lock(&threadMut);
        my_node = init_objThread(my_id, &cv_arr[my_id]);
        add_to_tail(sleeping_threads_queue,
                    my_node);
        if (cntThreads == len_QT(sleeping_threads_queue))
        {
            pathQueueSize = len_QP(path_queue);
            if (pathQueueSize == 0)
            {
                search_over = 1;
                pthread_mutex_unlock(&threadMut);
                for (i = 0; i < cntThreads; i++)
                {
                    pthread_cond_signal(&cv_arr[i]);
                }
                pthread_cond_signal(&search_over_cv);
                pthread_exit(NULL);
            }
        }
        pthread_cond_wait(&cv_arr[my_id], &threadMut);
        if (search_over)
        {
            pthread_mutex_unlock(&threadMut);
            pthread_exit(NULL);
        }
        pthread_mutex_unlock(&threadMut);
    }
}
int main(int argc, char *argv[])
{
    long i;
    int rc;
    char *startPath;
    char *ptr;
    DIR *dir;

    if (argc != 4)
    {
        printf("ERROR- Invalid number of arguments.");
        exit(1);
    }
    startPath = argv[1];
    st = argv[2];
    cntThreads = strtol(argv[3], &ptr, 10);

    dir = opendir(startPath);
    if (!dir)
    {
        printf("ERROR- dir cannot be searched!");
        exit(1);
    }

    pthread_t threads[cntThreads];
    cv_arr = malloc(sizeof(pthread_cond_t) * cntThreads);
    if (cv_arr == NULL)
    {
        printf("ERROR");
        exit(1);
    }
    sleeping_threads_queue = malloc(sizeof(struct QT));
    if (sleeping_threads_queue == NULL)
    {
        printf("ERROR");
        exit(1);
    }
    sleeping_threads_queue->head = sleeping_threads_queue->tail = NULL;
    path_queue = malloc(sizeof(struct QP));
    if (path_queue == NULL)
    {
        printf("ERROR.");
        exit(1);
    }
    path_queue->head = path_queue->tail = NULL;
    inserQP(path_queue, startPath, 1);
    pthread_mutex_init(&threadMut, NULL);
    pthread_mutex_init(&path_queue_mutex, NULL);
    pthread_mutex_init(&search_over_mutex, NULL);
    pthread_cond_init(&all_threads_created, NULL);
    pthread_cond_init(&search_over_cv, NULL);
    for (i = 0; i < cntThreads; i++)
    {
        pthread_cond_init(&cv_arr[i], NULL);
    }
    for (i = 0; i < cntThreads; i++)
    {
        rc = pthread_create(&threads[i], NULL, search_thread_func, (void *)i);
        if (rc)
        {
            printf("ERROR  "
                   "%s\n",
                   strerror(rc));
            exit(1);
        }
    }
    for (i = 0; i < cntThreads; i++)
    {
        objThread *node = init_objThread(i, &cv_arr[i]);
        add_to_tail(sleeping_threads_queue, node);
    }
    pthread_mutex_lock(&threadMut);
    objThread *StartThreads = insertQT(sleeping_threads_queue);
    pthread_mutex_unlock(&threadMut);
    while (cntThreads > NumThreads)
    {
        pthread_mutex_lock(&threadMut);
        pthread_cond_wait(&all_threads_created, &threadMut);
        pthread_mutex_unlock(&threadMut);
    }
    pthread_mutex_lock(&threadMut);
    pthread_cond_signal(StartThreads->cv);
    pthread_mutex_unlock(&threadMut);
    pthread_mutex_lock(&search_over_mutex);
    pthread_cond_wait(&search_over_cv, &search_over_mutex);
    pthread_mutex_unlock(&search_over_mutex);
    for (i = 0; i < cntThreads; i++)
    {
        rc = pthread_join(threads[i], NULL);
        if (rc)
        {
            printf("ERROR  "
                   "%s\n",
                   strerror(rc));
            exit(1);
        }
    }
    pthread_mutex_destroy(&threadMut);
    pthread_mutex_destroy(&path_queue_mutex);
    pthread_mutex_destroy(&search_over_mutex);
    for (i = 0; i < cntThreads; i++)
    {
        pthread_cond_destroy(&cv_arr[i]);
    }
    pthread_cond_destroy(&all_threads_created);
    pthread_cond_destroy(&search_over_cv);
    free_QP(path_queue);
    free_QT(sleeping_threads_queue);
    free(path_queue);
    free(sleeping_threads_queue);
    free(cv_arr);
    printf("Done searching, found %d files\n", finalCnt);
    exit(0);
}
