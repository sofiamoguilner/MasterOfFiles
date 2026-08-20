#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>


typedef enum { REEMPLAZO_LRU, REEMPLAZO_CLOCKM } t_reemplazo;

typedef struct {
    size_t tam_memoria;
    size_t page_size;
    size_t frames;
    uint32_t retardo;
    t_reemplazo algORITMO;
} Ram;

typedef struct {
    bool libre;
    char* base;
    char* file;
    char* tag;
    uint32_t pagina;
    bool dirty;
    uint8_t U, M; //bit de Uso y bit Modificado
    uint64_t last_used;
} Marco;

/* Inicialización y destrucción de la memoria interna */
int  inicializar_memoria(Ram* info);
void destrozar_memoria(void);

/* Contexto actual (para logs) */
void memoria_set_context(int query_id); //para reconocer el id del query actual

/* Acceso de datos */
int  traer_pagina(char* file, char* tag, uint32_t nro_pag, int storage_fd);
int  acceder_memoria(char* file, char* tag, size_t base, size_t size, bool escribir,
                     void* entrada, void* salida, int storage_fd);

int  flush_filetag(char* file, char* tag, int storage_fd);

int flush_frame(size_t i, int storage_fd);

int flush_todos_filetags_modificados(int storage_fd);
/* Comunicación con Storage */
int  lectura_pagina_storage(int fd, char* file,  char* tag,
                            uint32_t nro_pag, void* out);
int  escribir_pagina_storage(int fd,  char* file,  char* tag,
                             uint32_t nro_pag, void* in, size_t len);

/* Utilidades internas */
uint64_t milisegundos_actual(void);
void aplicar_retardo_memoria(void);

bool frame_matchea(Marco* f, char* file, char* tag, uint32_t nro_pag);

//void flush_paginas_de_query(int query_id, int socket_storage);

extern int CONTEXT_QUERY_ID;
extern size_t FRAME_COUNT;
extern Marco* frames;

