#ifndef PROTOCOLO_H
#define PROTOCOLO_H
#include <utils/hello.h>

int parse_size_t_safe(const char* str, size_t* out);

// Códigos de operación que viajan en los sockets
// Sirven para identificar qué tipo de mensaje es

enum op_code{
    // Mensajes genéricos
    OP_MENSAJE,   // string genérico (enviar_mensaje / recibir_mensaje)
    OP_ENVIAR_PATH,   // (si querés distinguir del OP_MENSAJE)

    // Worker <-> Storage
    OP_PEDIR_TAM_BLOQUE,   // Worker -> Storage: pide BLOCK_SIZE
    OP_RESP_TAM_BLOQUE  = 4,   // Storage -> Worker: responde BLOCK_SIZE (int)

    // Handshakes de identidad
    OP_HELLO_QUERY      = 10,  // cliente avisa que es Query Control
    OP_HELLO_WORKER     = 11,  // cliente avisa que es Worker

    // Worker <-> Master (envío de ID de Worker)
    OP_WORKER_ID        = 2001, // Worker -> Master: envía int id_worker

    OP_ST_READ_BLOCK      = 200,
    OP_ST_WRITE_BLOCK     = 201,
    OP_ST_FLUSH_FILETAG   = 202,
    OP_ST_COMMIT_FILETAG  = 203,
    OP_ST_DELETE_FILETAG  = 204,
    OP_ST_TRUNCATE_FILETAG= 205,
    OP_ST_OK              = 210,
    OP_ST_ERR             = 211
};
// Códigos de operación para los mensajes entre procesos
//INSTRUCCIONES
#define OP_CREATE           100
#define OP_TRUNCATE         102
#define OP_WRITE            103
#define OP_READ             104
#define OP_TAG              105
#define OP_COMMIT           106
#define OP_FLUSH            107
#define OP_DELETE           108
#define OP_END              109

// Respuestas
#define OP_RESP_OK          100
#define OP_RESP_ERROR       101

//planificacion master
#define OP_DESALOJAR_QUERY  301   // Master a Worker
#define OP_QUERY_DESALOJADA 302   // Worker a Master devuelve PC y qid desalojado
#define OP_REANUDAR_QUERY   303   // Master a Worker para reanudar desde PC
#define OP_QUERY_DESCONECTADA 304 // Master a Worker notifica que el Query Control se desconectó
#define OP_ESTADO_PAUSADO 305


#define OP_RESP_READ_OK   1020
#define OP_RESP_READ_ERR  1021
#define OP_FIN_QUERY  500




#endif