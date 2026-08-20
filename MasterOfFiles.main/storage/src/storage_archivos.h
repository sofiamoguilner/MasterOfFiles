#ifndef STORAGE_ARCHIVOS_H
#define STORAGE_ARCHIVOS_H

#include <storage_gestion_estructuras.h>
//#include <sincronizacion.h>
/*
extern pthread_mutex_t mutex_bitmap;
extern pthread_mutex_t mutex_bloques;
extern pthread_mutex_t mutex_listas_archivos;
*/

void log_acceso_bitmap(uint32_t numeroBloque, uint32_t estadoBloque);

int32_t bitmap_encontrar_bloque_libre();

void bitmap_mostrar_por_pantalla();

void bitmap_marcar_bloque_libre(uint32_t numeroBloque);

void bitmap_marcar_bloque_ocupado(uint32_t numeroBloque);

#endif