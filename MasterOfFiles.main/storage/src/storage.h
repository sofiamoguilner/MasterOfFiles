#ifndef ESTRUCTURAS_STORAGE_H
#define ESTRUCTURAS_STORAGE_H

// Bibliotecas creadas
#include <storage_gestion_estructuras.h> 
#include <storage_inicializar.h>
#include <f_fresh_stat.h> 
#include <storage_archivos.h> //maneja bitmap
#include <storage_conexiones.h>
#include <operaciones_storage.h>

//  Variables globales
t_log* storage_logger;
t_log* storage_log_debug;
storage_config configuracion;

t_superbloque superbloque;

t_bitmap* bitmap;
t_config* hash_index_config;

int fd_storage;
int fd_cliente;

t_list* workers;

t_list* lista_files; //files creados
t_list* lista_name_file; // list<name_file>
t_list* lista_bloques_fisicos; // posiblemente este de mas 
int cant_block;
// directorios y archivos nativos
char* directorio_raiz;
char* directorio_blocks_fisicos;
char* directorio_files;

char* ruta_superbloque;
char* ruta_hash_index;
char* ruta_bitmap;


pthread_mutex_t mutex_bitmap;
pthread_mutex_t mutex_bloques;
pthread_mutex_t mutex_listas_archivos;
pthread_mutex_t mutex_listas_bloques_fisicos;
pthread_mutex_t mutex_workers;

#endif