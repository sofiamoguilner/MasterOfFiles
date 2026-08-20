#include <stdio.h>
#include <stdlib.h>
#include <string.h>  
#include <commons/log.h>
#include <commons/config.h>
#include <utils/hello.h> // declara crear_conexion, enviar_mensaje, liberar_conexion
#include <utils/protocolo.h> 


// logger global usado por utils
//t_log* logger = NULL;


static void uso(const char* prog){
    fprintf(stderr, "Uso: %s <query.config> <path_query> <prioridad>\n", prog);
}


int main(int argc, char** argv){
    if(argc < 4){ uso(argv[0]); return 1; }


    // argv[0] = nombre del programa
    const char* ruta_cfg   = argv[1]; // archivo de configuración
    const char* path_query = argv[2]; // archivo de query a ejecutar
    int prioridad = atoi(argv[3]);

    //creamos el config
    t_config* cfg = config_create((char*)ruta_cfg);
    if(!cfg){ fprintf(stderr, "No pude abrir %s\n", ruta_cfg); return 1; }


    char* ip_master = config_get_string_value(cfg, "IP_MASTER");
    char* puerto_master = config_get_string_value(cfg, "PUERTO_MASTER");


    logger = log_create("query.log", "QUERY", true, LOG_LEVEL_DEBUG);

    //conexion con Master
    int socket_master = crear_conexion(ip_master, puerto_master);
    if(socket_master < 0){
        log_error(logger, "No pude conectar al Master %s:%s", ip_master, puerto_master);
        config_destroy(cfg);
        log_destroy(logger);
        return 1;
    }
    log_info(logger, "## Conexión al Master exitosa. IP: %s, Puerto: %s", ip_master, puerto_master);

    // Handshake
    int op_query = OP_HELLO_QUERY;
    send(socket_master, &op_query, sizeof(op_query), 0);


    // Path y Prioridad
    t_paquete* solicitud = crear_paquete();
    solicitud->codigo_operacion = OP_ENVIAR_PATH;
    agregar_a_paquete(solicitud, (void*)path_query, strlen(path_query)+1); // strlen(path_query) devuelve la cantidad de caracteres sin contar el '\0' (por eso el +1)
    agregar_a_paquete(solicitud, &prioridad, sizeof(int)); // prioridad 
    enviar_paquete(solicitud, socket_master); //serializo el paquete y lo envio a master

    log_info(logger, "## Solicitud de ejecución de Query: %s, prioridad: %d",path_query, prioridad);

    eliminar_paquete(solicitud); //libera la memoria usada por el paquete
    
     // Esperar mensajes/respuestas del Master
    while(1) {
        int cod_op = recibir_operacion(socket_master); //bloquea hasta recibir el codigo de operacion
        if(cod_op <= 0) {
            log_error(logger, "Conexión cerrada por el Master");
            break;
        }

        switch(cod_op) {
            case OP_MENSAJE: {
                t_list* campos = recibir_paquete(socket_master);
                if (!campos || list_size(campos) < 1) {
                    log_error(logger, "OP_MENSAJE mal formado desde Master");
                    if (campos) list_destroy_and_destroy_elements(campos, free);
                    break;
                }

                char* mensaje = list_get(campos, 0);  // string que mandó el Master

                log_info(logger, "## Lectura realizada: %s", mensaje);

                list_destroy_and_destroy_elements(campos, free);
                break;
            }

            case OP_FIN_QUERY: {
                t_list* campos = recibir_paquete(socket_master);
                if (!campos || list_size(campos) < 1) {
                    log_error(logger, "OP_FIN_QUERY mal formado desde Master");
                    if (campos) list_destroy_and_destroy_elements(campos, free);
                    liberar_conexion(socket_master);
                    config_destroy(cfg);
                    log_destroy(logger);
                    return 1;
                }

                char* motivo = list_get(campos, 0);

                log_info(logger, "## Query Finalizada - %s", motivo);

                list_destroy_and_destroy_elements(campos, free);
                liberar_conexion(socket_master);
                config_destroy(cfg);
                log_destroy(logger);
                return 0;
            }


            default:
                log_warning(logger, "Operación desconocida recibida: %d", cod_op);
            break;
        }
    }

    // Liberar recursos si salimos del loop
    liberar_conexion(socket_master);
    config_destroy(cfg);
    log_destroy(logger);
    return 0;
}

