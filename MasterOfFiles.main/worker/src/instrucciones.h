// esto es para que el archivo se use solo 1 vez durante la compilacion
#ifndef INSTRUCCIONES_H
#define INSTRUCCIONES_H

#include <commons/log.h>
#include <stddef.h> // para el srtuct de instruccion
#include <stdbool.h> 


extern t_log* logger;

// Convierte string en size_t de forma segura
int parse_size_t_safe(const char* txt, size_t* out); //hay que desarrollarlo

// Estructura para instruccion parseada
typedef struct 
{
    char comando[32];
    char arg1[128];
    char arg2[128];
    char arg3[256];

} Instruccion;

size_t ciclo_instruccion(char* txt, int id_query, size_t pc, int socket_master, int socket_storage, int block_size, t_log* logger);

// Ejecuta una instrucción individual (ej: CREATE, WRITE, etc.)
void ejecutar_instruccion(Instruccion* inst, int id_query, int socket_master, int socket_storage, int block_size, t_log* logger);

// Lee un archivo de Query y ejecuta todas sus instrucciones línea por línea
int procesar_query(char* ruta, int id_query, size_t pc_inicial, int socket_master, int socket_storage, int block_size, t_log* logger);


// Implementa todas las instruciones 
int ejecutar_Create(Instruccion* inst, int socket_storage, t_log* logger);
int ejecutar_Truncate( Instruccion* inst, int socket_storage, int block_size, t_log* logger);
int ejecutar_Write( Instruccion* inst, int socket_storage, t_log* logger);
int ejecutar_Read( Instruccion* inst, int socket_master, int socket_storage, t_log* logger);
int ejecutar_Tag( Instruccion* inst, int socket_storage, t_log* logger);
int ejecutar_Commit( Instruccion* inst, int socket_storage, t_log* logger);
int ejecutar_Flush( Instruccion* inst, int socket_storage, t_log* logger);
int ejecutar_Delete( Instruccion* inst, int socket_storage, t_log* logger);
int ejecutar_End(int id_query, int socket_master, t_log* logger);

void remove_path_for_qid(int qid);

extern volatile bool query_cancelada;//Permite que cualquier función del módulo instrucciones sepa si la query fue cancelada
//extern t_log* logger;

#endif //fin del bloque protegido