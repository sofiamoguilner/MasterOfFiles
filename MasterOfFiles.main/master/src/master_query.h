#ifndef MASTER_QUERY_H_
#define MASTER_QUERY_H_

#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <netdb.h>
#include <commons/log.h>

#include <master_estructuras.h>
#include <utils/hello.h>

uint32_t generar_qid();
t_query *crear_query(char* path, int prioridad, int sock);
char* leer_archivo_completo(char* path);

char* path_nombre_archivo(const char* ruta);
char* path_unir_directorio_archivo(const char* directorio, const char* nombre);

void manejar_desconexion_qc(int socketQC);

extern void enviar_fin_query(t_query* q, char* motivo);
void eliminar_query_por_socket(uint32_t socketQuery);


#endif