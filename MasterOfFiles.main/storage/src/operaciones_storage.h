#ifndef OPERACIONES_STORAGE_H
#define OPERACIONES_STORAGE_H

#include <storage_gestion_estructuras.h>
#include <stdbool.h>
#include <storage_archivos.h>
#include <f_fresh_stat.h>


char* direccion_metadata(char* file, char* tag);
char* crear_nombre_bloque_logico(int numero);  // 000000, 000001, etc


// Retorna p = "home/utnso/storage/files/nameFILE"
char* ruta_file(char* nameFile);

//Retorna p = "home/utnso/storage/files/nameFILE/nameTAG"
char* ruta_tag(char* nameFile, char *nameTag);

char* ruta_blockLogico(char* nameFile, char* nameTag,char* nameBloque);

int operacion_create_file(char* nameFile, char* nameTag);

int operacion_truncate_file(int qid, char* nameFile, char* nameTag, int tamanio_bytes);
void copiar_archivo (char *archivo_origen,  char *archivo_destino);


// En operaciones_storage.h
int reasignar_bloque_fisico_compartido(int qid, char* nameFile, char* nameTag, t_tag* un_tag, 
                                      t_block_logico* bl, t_block_fisico* bf, 
                                      char* contenido_original, char* contenido_nuevo);

/*
    Crea un file destino y su ruta sino existe
    Copia metadata del tag origen en tag destino
    Copia la lista de bloques logicos y 
    linkea esos con los block fisicos asociados
    Al block fisico se suma 1 por el link creado

    Devuelve 
        -1 File:Tag origen no existe
         0 Exitoso
*/
int operacion_tag_file(int qid, char* nameFileOrigen , char* nameTagOrigen, char* nameFileDestino,char* nameTagDestino);
/*
    Devuelve
        -2 No se encuentra en el hash index
        -1 file:tag inexistente
         1 file:tag ya en estado commited
         0 exitoso
*/
int operacion_commit_tag(char* nameFile, char* nameTag);

/*
    Escribe en el bloque fisico 
    Devuelve 
         0 exitoso
         1 File:Tag inexistente
        -1 estado de metadata COMMITED
        
*/
int operacion_write_bloque(int qid, char* nameFile , char* nameTag, t_block_logico* _bl, char* contenido, size_t len);

/*
    nameFile = "arch1"
    nameTag ="tag_1"
    nroBlockLogico = "000000" tambien es el nameBlockLogico de t_block_logico
*/
char* operacion_read_bloque(char* nameFile,char*nameTag,char* nroBlockLogico);

void operacion_eliminar_tag(int qid, char* nameFile, char* nameTag);

//void listar_directorios(const char *ruta);

// funciones auxiliares 
char* _nameBloque(int nroBloque); // "block0000"
int buscar_index_file(char* nameFile);
int buscar_index_tag(char* nameFile, char* nameTag);
int buscar_index_blockFisico(char* nameBloqueFisico);

t_file* buscar_file(char* nameFile);
t_tag* buscar_tag(char* nameFile, char* nameTag);


void copiar_archivo( char *archivo_origen,  char *archivo_destino);

// No utilizado , modificar
void actualizar_hash_index_config(char* contenido_viejo,char* contenido_nuevo ,int index); 

int comparar_enteros(const void* a, const void* b);
/*
    Agrega al char** bloques fisicos usados 
    el nuevo bloque asignado 
    Devolviendo un char** ordenado de menor a mayor

*/
void agregar_bloque_usado(char*** array, int nro_bloque_asigando);
/*
    ACtualiza el archivo metadata.config con los nuevos valores
    Devuelve
         0 Exitoso
        -1 No se pudo encontrar archivo .config
*/
int actualizar_metadata(char* ruta_tag, t_metadata* _metadata);
/*
    Crea un link del block logico a block fisico
    Return
         0   Exito
        -1  Enlace no creado
*/
int linkear_bl_a_bf(char* nameFile,char* nameTag, char* nameBL,char* nameBF);
/*
    Busca bloque fisico en el hash config
    para saber a que bloque fisico esta asociado el bloque logico

    Devuelve
        NULL Si no esta registrado en el archivo config hash_index
             o no esta en la lista de bloques fisicos
        t_block_fisico* un puntero a bloque fisico asociado
*/
t_block_fisico* buscar_bf_asociado(char* hash_bock_logico);
/*
    Busca en la lista segun el nombre
    Devuelve 
         NRO de bloques asociados a este bfisico
         0 error al encontrar bloque fisico
*/
int blocks_fisicas_asociados(char* nameBF);

void procesar_bloque_commit(char* file, char* tag, t_block_logico* bl);


char* crear_nombre_bloque_logico(int numero);

bool esta_en_array(char** array, char* valor);

void quitar_del_array_blocks(char*** array_ptr, char* valor);
#endif