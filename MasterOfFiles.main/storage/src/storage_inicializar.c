// Bibliotecas creadas

#include <storage_inicializar.h>

storage_config leer_configuracion(char* archivo_config){

    t_config* config = config_create(archivo_config);
    ensure(config != NULL, "ERROR: No pude abrir el archivo de configuración. Revisá la ruta/permissions.");

    storage_config configuracion; 
    configuracion.puerto_escucha = config_get_string_value(config,"PUERTO_ESCUCHA");

    char* var_auxiliar =config_get_string_value(config,"FRESH_START");
    if (strcmp(var_auxiliar,"TRUE") == 0)
    {
        configuracion.fresh_start = true;
    }
    else{
        configuracion.fresh_start = false;
    }
    //free(var_auxiliar);

    configuracion.punto_montaje = config_get_string_value(config,"PUNTO_MONTAJE");
    configuracion.retardo_operacion = config_get_int_value(config,"RETARDO_OPERACION");
    configuracion.retardo_acceso = config_get_int_value(config,"RETARDO_ACCESO_BLOQUE");
    configuracion.log_level = config_get_string_value(config,"LOG_LEVEL");
    
    // lo comente pq puede generar perdida de memoria free(config);
    //config_destroy(config);
    return configuracion;
}

int inicializar_config(int argc, char* argv[]){
    if (argc < 2) {
        fprintf(stderr, "Uso: %s storage.config\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    // leer configuracion del config
    configuracion = leer_configuracion(argv[1]);
    return 0;
}

void inicializar_logs(){
    // log minimos
    storage_logger = log_create("storage.log", "STORAGE", true, log_level_from_string(configuracion.log_level));
    ensure(storage_logger != NULL, "ERROR: No pude crear el logger.");

    // log_debug nos servira para comprobar cosas y no mezclarlo con los log minimos
    storage_log_debug = log_create("storage_debug.log","STORAGE",true,LOG_LEVEL_TRACE);
    ensure(storage_log_debug != NULL, "ERROR: No pude crear el logger.");
  
}

void inicializar_storage(int argc, char* argv[]){
    inicializar_config(argc,argv);
    inicializar_logs();
    mostrar_config();
}

void mostrar_config(){
    log_debug(storage_log_debug,"Config leido :");
    log_trace(storage_log_debug,"Puerto escucha: %s",configuracion.puerto_escucha);
    log_trace(storage_log_debug,"Fresh_start : %s", configuracion.fresh_start ? "true":"false");
    log_trace(storage_log_debug,"Punto montaje : %s",configuracion.punto_montaje);
    log_trace(storage_log_debug,"Retardo operacion : %d",configuracion.retardo_operacion);
    log_trace(storage_log_debug,"Retardo acceso al bloque : %d", configuracion.retardo_acceso);
    log_trace(storage_log_debug,"Log level : %s \n", configuracion.log_level);

}
