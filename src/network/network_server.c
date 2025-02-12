#include "network.h"

int is_running = 0;

struct network_server *network_server_new(
        struct network_conf *config,
        void (*_get)(const struct string_st *str, struct string_st *res),
        int (*_send)(const struct string_st *str)) {
    struct network_server *res = skr_malloc(sizeof(struct network_server));

    res->config = config;
    res->_get = _get;
    res->_send = _send;

    res->_socket = socket(config->domain, config->service, config->protocol);
    int option = 1;
    setsockopt(res->_socket, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
    res->server_address = (struct sockaddr_in) {};
    res->client_address = (struct sockaddr_in) {};
    res->hosts = list_new();

    if (res->_socket == 0) {
        perror("Failed to connect socket...\n");
        exit(1);
    }
    res->server_address.sin_family = config->domain;
    res->server_address.sin_addr.s_addr = htonl(config->interface);
    res->server_address.sin_port = htons(config->port);
    if ((bind(res->_socket, (struct sockaddr *) &res->server_address, sizeof(res->server_address))) < 0) {
        perror("Failed to bind socket...\n");
        exit(1);
    }
    if ((listen(res->_socket, config->backlog)) < 0) {
        perror("Failed to start listening...\n");
        exit(1);
    }
    return res;
}
void network_server_free(struct network_server *res) {
    if (is_running) is_running = 0;
    list_free(res->hosts);
    skr_free(res);
}


void network_server_accept(int client_socket, struct network_server *server) {
    char flag = 0;
    char flag_res = 0;
    int send_next = 0;
    struct string_st *msg = string_new();
    struct string_st *res_msg = string_new();

    network_read(client_socket, msg, &flag);
    if (flag & NET_CONNECTIONS) {
        flag_res |= NET_CONNECTIONS;
        if (flag & NET_SEND) {
            if (msg->size == 0) {
                char *data = inet_ntoa(server->client_address.sin_addr);
                string_resize(msg, strlen(data));
                memcpy(msg->data, data, msg->size);
            }
            send_next = 1;
            for (size_t i = 0; i < server->hosts->size; i++) {
                if (string_cmp(msg, server->hosts->data[i]->data) == 0) {
                    send_next = 0;
                    break;
                }
            }
            if (send_next) {
                list_add_new(server->hosts, STRING_TYPE);
                string_set(server->hosts->data[server->hosts->size - 1]->data, msg);
            }
        }
        if (flag & NET_GET) {
            list_get_tlv(server->hosts, res_msg);
        }
    }

    if (flag & NET_DATA) {
        flag_res |= NET_DATA;
        if (flag & NET_SEND) {
            if (server->_send != NULL) send_next = server->_send(msg);
        }
        if (flag & NET_GET) {
            if (server->_get != NULL) server->_get(msg, res_msg);
        }
    }
    if (flag & NET_GET) {
        flag_res |= NET_GET;
        flag_res |= NET_RESPONSE;
        network_send(client_socket, res_msg, flag_res);
    }
    close(client_socket);
    if ((flag & NET_SEND) && send_next) {
        network_server_send(server, msg, flag);
    }
    string_free(msg);
    string_free(res_msg);
}
void *network_server_init(void *arg) {
    struct network_server *server = arg;
    socklen_t address_length = sizeof(server->client_address);
    while (is_running) {
        int client_socket = accept(server->_socket, (struct sockaddr *) &server->client_address, &address_length);
        network_server_accept(client_socket, server);
    }
    return NULL;
}

void network_server_start(struct network_server *res) {
    if (is_running) return;
    is_running = 1;
    pthread_t server_thread;
    pthread_create(&server_thread, NULL, network_server_init, res);
    network_server_connect(res);
}
void network_server_close() {
    is_running = 0;
}

void network_server_connected(struct network_server *res) {
    struct string_st *msg = string_new();
    struct string_st *res_msg = string_new();
    network_server_get(res, msg, NET_CONNECTIONS, res_msg);
    if (res_msg->size != 0) {
        list_set_tlv_self(res->hosts, res_msg, STRING_TYPE);
    }
    string_free(msg);
    string_free(res_msg);
}
void network_server_connect(struct network_server *res) {
    list_add_new(res->hosts, STRING_TYPE);
    string_set_str(res->hosts->data[res->hosts->size - 1]->data, "127.0.0.1", 9);

    struct string_st *msg = string_new();
    network_server_send(res, msg, NET_CONNECTIONS);
    string_free(msg);
}

void network_server_get(struct network_server *res, const struct string_st *msg, char flag, struct string_st *res_msg) {
    char res_flag;
    struct network_client *client = network_client_new();
    network_client_set_config(client, res->config);
    for (size_t i = 0; i < res->hosts->size; i++) {
        res_flag = 0;
        network_client_connect(client, res->hosts->data[i]->data);
        network_client_get(client, msg, flag, res_msg, &res_flag);
        network_client_close(client);
        if ((res_flag & NET_ERROR) == 0) return network_client_free(client);
    }
    string_clear(res_msg);
}
void network_server_send(struct network_server *res, const struct string_st *msg, char flag) {
    struct network_client *client = network_client_new();
    network_client_set_config(client, res->config);
    for (size_t i = 0; i < res->hosts->size; i++) {
        network_client_connect(client, res->hosts->data[i]->data);
        network_client_send(client, msg, flag);
        network_client_close(client);
    }
    network_client_free(client);
}
