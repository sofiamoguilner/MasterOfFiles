#ifndef UTILS_HELLO_H
#define UTILS_HELLO_H

#include <string.h>
#include <commons/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>

#include <commons/log.h>
#include <commons/collections/list.h>
#include <commons/config.h>
#include <commons/string.h>
#include <assert.h>

#include <utils/protocolo.h>

#define PUERTO "10000"
extern t_log* logger;

int iniciar_servidor(const char*);
int esperar_cliente(int socket_servidor);

int recibir_operacion(int socket_cliente);
void* recibir_buffer(int* size, int socket_cliente);
char* recibir_mensaje(int socket_cliente);
t_list* recibir_paquete(int socket_cliente);
void ensure(bool cond, const char* msg);

void iterator(char* value);



// ------ cliente ------

typedef enum
{
	MENSAJE,
	PAQUETE,
	HANDSHAKE_WORKER
}op_code;

typedef struct
{
	int size;
	void* stream;
} t_buffer;

typedef struct
{
	op_code codigo_operacion;
	t_buffer* buffer;
} t_paquete;

int crear_conexion(char* ip, char* puerto);
void enviar_mensaje(char* mensaje, int socket_cliente);
t_paquete* crear_paquete(void);
void agregar_a_paquete(t_paquete* paquete, void* valor, int tamanio);
int enviar_paquete(t_paquete* paquete, int socket_cliente);
void liberar_conexion(int socket_cliente);
void eliminar_paquete(t_paquete* paquete);


int enviar_paquete_simple(int socket_fd, int opcode, char* instr);

int split_file_tag(const char* filetag, char** out_file, char** out_tag);

#endif // UTILS_HELLO_H