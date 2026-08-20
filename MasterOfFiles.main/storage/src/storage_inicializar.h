#ifndef STORAGE_INICIALIZAR_H
#define STORAGE_INICIALIZAR_H

#include <storage_gestion_estructuras.h>

// logs y config
storage_config leer_configuracion(char* archivo_config);
int inicializar_config(int argc, char* argv[]);
void inicializar_logs();
void inicializar_storage(int argc, char* argv[]);
void mostrar_config();


#endif