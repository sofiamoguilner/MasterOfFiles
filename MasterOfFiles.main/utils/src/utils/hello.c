#include <utils/hello.h>
t_log* logger = NULL; 

//Funciones del Servidor

int iniciar_servidor(const char* puerto){

	int socket_servidor;

	struct addrinfo hints, *servinfo;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	getaddrinfo(NULL, puerto, &hints, &servinfo);

	// Creamos el socket de escucha del servidor con fd_escucha o socket_servidor
	socket_servidor = socket(servinfo->ai_family,
							servinfo->ai_socktype,
							servinfo->ai_protocol);

	int err = 0;
    err = setsockopt(socket_servidor, SOL_SOCKET, SO_REUSEPORT, &(int){1}, sizeof(int));
    // Asociamos el socket a un puerto
    err = bind(socket_servidor, servinfo->ai_addr, servinfo->ai_addrlen);
    // Escuchamos las conexiones entrantes
    err = listen(socket_servidor, SOMAXCONN);

	if(err != 0)
		log_error(logger, "Error con el puerto %s: %d", puerto, err);

	freeaddrinfo(servinfo);
	log_trace(logger, "Listo para escuchar a mi cliente");

	return socket_servidor;
}

//Esperamos que el cliente se conecte
int esperar_cliente(int socket_servidor){

	// Aceptamos un nuevo cliente --> accept()
	int socket_cliente = accept(socket_servidor, NULL, NULL);
	log_info(logger, "Se conecto un cliente!");

	return socket_cliente;
}

//Recibimos la operacion del socket cliente
int recibir_operacion(int socket_cliente)
{
	int cod_op;
	if(recv(socket_cliente, &cod_op, sizeof(int), MSG_WAITALL) > 0)
		return cod_op;
	else
	{
		close(socket_cliente);
		return -1;
	}
}

void* recibir_buffer(int* size, int socket_cliente)
{
	void * buffer;

	recv(socket_cliente, size, sizeof(int), MSG_WAITALL);
	buffer = malloc(*size);
	recv(socket_cliente, buffer, *size, MSG_WAITALL);

	return buffer;
}
char* recibir_mensaje(int socket_cliente)
{
    int size = 0;
    char* buffer = recibir_buffer(&size, socket_cliente); // malloc interno

    if (!buffer) return NULL;
    //log_info(logger, "Me llego el mensaje: %s", buffer);

    return buffer; // el caller debe hacer free(msg);
}

t_list* recibir_paquete(int socket_cliente)
{
	int size;
	int desplazamiento = 0;
	void * buffer;
	t_list* valores = list_create();
	int tamanio;

	buffer = recibir_buffer(&size, socket_cliente);
	while(desplazamiento < size)
	{
		memcpy(&tamanio, buffer + desplazamiento, sizeof(int));
		desplazamiento+=sizeof(int);
		char* valor = malloc(tamanio);
		memcpy(valor, buffer+desplazamiento, tamanio);
		desplazamiento+=tamanio;
		list_add(valores, valor);
	}
	free(buffer);
	return valores;
}

void ensure(bool cond, const char* msg) {
    if (!cond) { fprintf(stderr, "%s\n", msg); exit(EXIT_FAILURE); }
}
//Funciones del cliente

void* serializar_paquete(t_paquete* paquete, int bytes)
{
	void * magic = malloc(bytes);
	int desplazamiento = 0;

	memcpy(magic + desplazamiento, &(paquete->codigo_operacion), sizeof(int));
	desplazamiento+= sizeof(int);
	memcpy(magic + desplazamiento, &(paquete->buffer->size), sizeof(int));
	desplazamiento+= sizeof(int);
	memcpy(magic + desplazamiento, paquete->buffer->stream, paquete->buffer->size);
	desplazamiento+= paquete->buffer->size;

	return magic;
}

int crear_conexion(char *ip, char* puerto)
{
	struct addrinfo hints;
	struct addrinfo *server_info;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	getaddrinfo(ip, puerto, &hints, &server_info);

	//Creamos el socket del cliente.
	int socket_cliente = socket(server_info->ai_family,
								server_info->ai_socktype,
								server_info->ai_protocol);

	//Conectamos el socket cliente con el server para establecer la conexion
	if (connect(socket_cliente,server_info->ai_addr,server_info->ai_addrlen) <0){
		printf("\nConexion fallida\n");
		return -1;
	}

	freeaddrinfo(server_info);

	return socket_cliente;
}

void enviar_mensaje(char* mensaje, int socket_cliente)
{
	t_paquete* paquete = malloc(sizeof(t_paquete));

	paquete->codigo_operacion = MENSAJE;
	paquete->buffer = malloc(sizeof(t_buffer));
	paquete->buffer->size = strlen(mensaje) + 1;
	paquete->buffer->stream = malloc(paquete->buffer->size);
	memcpy(paquete->buffer->stream, mensaje, paquete->buffer->size);

	int bytes = paquete->buffer->size + 2*sizeof(int);

	void* a_enviar = serializar_paquete(paquete, bytes);

	send(socket_cliente, a_enviar, bytes, 0);

	free(a_enviar);
	eliminar_paquete(paquete);
}


void crear_buffer(t_paquete* paquete)
{
	paquete->buffer = malloc(sizeof(t_buffer));
	paquete->buffer->size = 0;
	paquete->buffer->stream = NULL;
}

t_paquete* crear_paquete(void)
{
	t_paquete* paquete = malloc(sizeof(t_paquete));
	paquete->codigo_operacion = PAQUETE;
	crear_buffer(paquete);
	return paquete;
}

void agregar_a_paquete(t_paquete* paquete, void* valor, int tamanio)
{
	paquete->buffer->stream = realloc(paquete->buffer->stream, paquete->buffer->size + tamanio + sizeof(int));

	memcpy(paquete->buffer->stream + paquete->buffer->size, &tamanio, sizeof(int));
	memcpy(paquete->buffer->stream + paquete->buffer->size + sizeof(int), valor, tamanio);

	paquete->buffer->size += tamanio + sizeof(int);
}

int enviar_paquete(t_paquete* paquete, int socket_cliente)
{
	int bytes = paquete->buffer->size + 2*sizeof(int);
	void* a_enviar = serializar_paquete(paquete, bytes);

	int resultado = send(socket_cliente, a_enviar, bytes, 0);

	free(a_enviar);

	return resultado;
}

void eliminar_paquete(t_paquete* paquete)
{
	free(paquete->buffer->stream);
	free(paquete->buffer);
	free(paquete);
}

void liberar_conexion(int socket_cliente)
{
	close(socket_cliente);
}


int enviar_paquete_simple(int socket_fd, int opcode, char* instr) {
    t_paquete* p = crear_paquete();
    p->codigo_operacion = opcode;
    agregar_a_paquete(p, (void*)instr, strlen(instr) + 1);
    enviar_paquete(p, socket_fd);
    eliminar_paquete(p);
    return 0;
}


// ---- helper local para separar "<file:tag>" ----
int split_file_tag(const char* filetag, char** out_file, char** out_tag) {
    if (!filetag || !out_file || !out_tag) return -1;

    const char* sep = strchr(filetag, ':');
    if (!sep) return -1;

    size_t len_file = (size_t)(sep - filetag);
    size_t len_tag  = strlen(sep + 1);

    char* f = malloc(len_file + 1);
    char* t = malloc(len_tag + 1);
    if (!f || !t) {
        free(f); free(t);
        return -1;
    }

    memcpy(f, filetag, len_file);
    f[len_file] = '\0';
    memcpy(t, sep + 1, len_tag + 1);   // copia también el '\0'

    *out_file = f;
    *out_tag  = t;
    return 0;
}
