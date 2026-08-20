#ifndef WORKER_H
#define WORKER_H

#include <stddef.h>      // size_t
#include <commons/log.h> // t_log
#include <instrucciones.h>

extern int pc_actual;     
extern bool desalojado;


// Genera path completo del archivo de Query 
char* generar_path_query(char* path_queries, char* nombre_query);



int conexion_con_storage(char* ip_storage, char* puerto_storage);
int solicitud_block_size(int socket_storage, int id_worker);


int conexion_con_master(char* ip_master, char* puerto_master);


#endif // WORKER_H