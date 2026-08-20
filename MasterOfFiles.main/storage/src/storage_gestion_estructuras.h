#ifndef STORAGE_GESTION_H
#define STORAGE_GESTION_H

// Bibliotecas basicas
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h> // para access()
#include <errno.h>    // Para el manejo de errores
#include <sys/stat.h> // Para la función mkdir
#include <sys/types.h> // Para mode_t 
#include <sys/mman.h> // Para funciones MMap y MSync 
#include <fcntl.h> //uso funcin open 
#include <dirent.h> // para recorrer directorio 


//Bibliotecas commons
#include <commons/config.h>
#include <commons/log.h>
#include <commons/collections/list.h>
#include <commons/string.h>
#include <commons/bitarray.h> // Para el bitmap
#include <commons/txt.h> // servira para manejar archivos basico
#include <commons/crypto.h> // funcion crypto_md5()

//Bibliotecas creadas
#include <utils/hello.h>   
#include <utils/protocolo.h>


//Bibliotecas de red
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h> // Uso de hilos

#define SUPERBLOQUE_CFG "FS_SIZE=4096\nBLOCK_SIZE=128\n"
#define DIR_METADATA "%s/metadata.config"
#define DIR_LOGICAL_BLOCK "%s/logical_blocks"
#define BLOCK_LOGICO_INICIAL "000000.dat"
#define BLOCK_FISICO_INICIAL "block0000"
#define DIR_STORAGE_CONFIG "storage.config"

typedef struct 
{
    char* puerto_escucha;
    bool fresh_start;
    char* punto_montaje;
    int retardo_operacion;
    int retardo_acceso;
    char* log_level;
}storage_config;

typedef struct
{
    uint32_t FS_SIZE;
    uint32_t BLOCK_SIZE;
}t_superbloque;

typedef struct 
{
    char *direccion;
    uint32_t tamanio;
    t_bitarray *bitarray;
}t_bitmap;

typedef struct {
    int ID_WORKER;
    //int ID_QUERY_ASOCIAOD; // Sera necesario despues
    int FD_HILO;   
    pthread_t HILO;
} t_hilo_worker;

// --- FS estructuras

typedef enum{
    WORK_IN_PROGRESS,
    COMMITED
}t_estado;

typedef struct 
{
   char* path_block; // "block0000"
   char* contenido; // "00..."
   int blogico_asociados; // cant de bloques asosciados 
}t_block_fisico;

typedef struct 
{
    int tamanio_file;
    char** blocks_fisicos_usados;//lista a ordenar [0,1,2,3]
    t_estado estado;
}t_metadata;


//podriamos usar char** del commons/string.h char**
typedef struct 
{
    char* nombre_block; // "000000.dat"
    //char* bl_contenido;
    //char* dir_block_logico;
    char* bloque_fisico_asociado; // "block0000" o BLOCK_FISICO_INICIAL
}t_block_logico;


typedef struct
{
    char* name_tag; // "tag_1_0_0"
    t_metadata* metadata;
    t_list* lista_block_logicos; // lista de t_block_logico
    
}t_tag;

typedef struct 
{
    char* name_File;
    t_list* tags; // lista t_tag
}t_file;

extern t_list* lista_files; //files creados
extern t_list* lista_name_file; // lista de name_file list<char*>
extern t_list* lista_bloques_fisicos; 

extern int cant_block;

/*
struct hash_index_dictionary
{
    char* HASH_BLOCK;
    char* BLOCK_ASOCIADO; 
};
typedef struct hash_index_dictionary index_hash_block;
*/

//  Variables globales
extern t_log* storage_logger;
extern t_log* storage_log_debug;

extern storage_config configuracion;
extern t_superbloque superbloque;
extern  t_config* hash_index_config;

extern int fd_storage;
extern int fd_cliente;

extern int contador_worker;

extern t_bitmap* bitmap;

extern t_list* workers;

// var directorios y archivos nativos
extern char* directorio_raiz;
extern char* directorio_blocks_fisicos;
extern char* directorio_files;

extern char* ruta_superbloque;
extern char* ruta_hash_index;
extern char* ruta_bitmap;

// Mutex globales para bitmap y bloques fisicos
extern pthread_mutex_t mutex_bitmap;
extern pthread_mutex_t mutex_bloques;

// Miutex para listas
extern pthread_mutex_t mutex_listas_archivos;
extern pthread_mutex_t mutex_listas_bloques_fisicos;
extern pthread_mutex_t mutex_workers;

#endif
