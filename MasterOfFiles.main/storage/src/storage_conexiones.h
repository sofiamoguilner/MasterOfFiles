#ifndef STORAGE_CONEXIONES_H
#define STORAGE_CONEXIONES_H


#include <storage_gestion_estructuras.h>
//#include <sincronizacion.h>
#include <atencion_worker.h>

void iterator(char* value);
int iniciar_servidor_con_worker();
int esperando_worker(int socket_servidor);

int index_elemento_idHilo(unsigned long id_hilo); // -1 si no existe elemento
t_hilo_worker* list_buscar_x_id_hilo(unsigned long id_hilo ); //devuelve elemeto
int get_idWorker();
void* atender_worker(void* arg);


/*
// Mutex globales para bitmap y bloques fisicos
pthread_mutex_t mutex_bitmap;
pthread_mutex_t mutex_bloques;

// Miutex para listas
pthread_mutex_t mutex_listas_archivos;
pthread_mutex_t mutex_listas_bloques_fisicos;
pthread_mutex_t mutex_workers;
*/
#endif
