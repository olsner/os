#include "lwip/ip.h"
#include "lwip/tcp.h"
#include "http.h"

#define log_enabled 0
#define log(...) do { if (log_enabled) printf(__VA_ARGS__); } while (0)

typedef struct tcp_pcb tcp_pcb;

static tcp_pcb* server_pcb;

const char response[] =
"HTTP/1.0 200 OK\r\n"
"Content-Type: text/html\r\n"
"\r\n"
"<h2>Hello World!</h2>\r\n"
;

static err_t http_request(tcp_pcb* pcb, struct pbuf* p) {
	log("http[%d]: sending response.\n", pcb->remote_port);
	tcp_write(pcb, response, sizeof(response) - 1, 0);
	tcp_close(pcb);
	// TODO Forward error codes from tcp write/close
	return ERR_OK;
}

static err_t http_recv_cb(void* arg_, tcp_pcb* pcb, struct pbuf* p, err_t err) {
	if (err != ERR_OK) {
		log("http: recv_cb err=%d\n", err);
		tcp_recv(pcb, NULL);
		tcp_output(pcb);
		tcp_close(pcb);
		return err;
	}
	struct pbuf* arg = arg_;
	if (p) {
		size_t len = p->len;
		size_t total = (arg ? arg->tot_len : 0) + len;
		log("http[%d]: received %ld bytes (%ld total)\n", pcb->remote_port, len, total);
		if (arg) {
			pbuf_cat(arg, p);
		} else {
			tcp_arg(pcb, arg = p);
			p->tot_len = len;
		}
		tcp_recved(pcb, len);
		if (arg->tot_len) {
			http_request(pcb, arg);
			tcp_arg(pcb, NULL);
			pbuf_free(arg);
		}
	} else if (arg) {
		log("http: received %ld bytes without sending response\n", arg->tot_len);
		tcp_arg(pcb, NULL);
		pbuf_free(arg);
		tcp_close(pcb);
	}
	return ERR_OK;
}

static err_t http_accept_cb(void *arg, tcp_pcb *newpcb, err_t err) {
	log("http[%d]: accepted connection from %x, err=%d\n", newpcb->remote_port, newpcb->remote_ip.addr, err);
	if (err != ERR_OK) {
		return err;
	}

	tcp_recv(newpcb, http_recv_cb);
	// TODO tcp_err(newpcb, http_err_cb);
	tcp_accepted(server_pcb);
	return err;
}

err_t http_start(void) {
	server_pcb = tcp_new();
	err_t res = tcp_bind(server_pcb, IP_ADDR_ANY, 80);
	if (res != ERR_OK) {
		return res;
	}
	server_pcb = tcp_listen_with_backlog(server_pcb, 5);
	tcp_accept(server_pcb, http_accept_cb);

	return res;
}
