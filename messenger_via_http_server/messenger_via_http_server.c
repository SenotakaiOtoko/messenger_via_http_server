/**
 * @file 
 * @brief Файл определяет точку входа.
 * 
 * Файл содержит функцию main и основные переменные сервера
 */
#include "stdafx.h"
#include "mongoose.h"
#include "db_plugin.h"

/// Порт, который будет прослушивать сервер
static const char * s_http_port = "8000";
/// Структура, управляющая поведением файлового HTTP сервера
static struct mg_serve_http_opts s_http_server_opts;
/// Signal не докумментирован в mongoose, но активно используется
static int s_sig_num = 0;
/// Handler базы данных
static void *s_db_handle = NULL;
/// Путь к базе данных
static const char * s_db_path = "./../server_database.db";
/// Обрабатываемый api тип запроса
static const struct mg_str s_post_method = MG_MK_STR("POST");

/**
 * @brief Функция проверяет, начинается ли строка uri со строки prefix
 *
 * @param[in] uri Входящая строка
 * @param[in] prefix Строка-префикс
 * @retval 1 Строка uri начинается с строки prefix
 * @retval 0 В противном случае
 */
static int has_prefix(const struct mg_str * uri, const struct mg_str * prefix) {
  return uri->len >= prefix->len && memcmp(uri->p, prefix->p, prefix->len) == 0;
}

/**
 * @brief Функция сравнивает две строки на предмет равенства
 *
 * @param s1,s2 Сравниваемые строки
 * @retval 1 Строки равны
 * @retval 0 Строки не равны
 */
int is_equal(const struct mg_str * s1, const struct mg_str * s2) {
  return s1->len == s2->len && memcmp(s1->p, s2->p, s2->len) == 0;
}

/**
 * @brief Функция-обработчик сигнала
 *
 * @param sig_num Какой-то недокумментированный параметр
 */
static void signal_handler(int sig_num) {
  signal(sig_num, signal_handler);
  s_sig_num = sig_num;
}

/**
 * @brief Функция-обработчик событий
 * 
 * @param[in] nc Соединение, в котором возникло событие
 * @param[in] ev Номер события, определённый в mongoose.h, начинающийся с MG_EV_
 * @param[in] ev_data Указатель на данные события. Данные различаются для всех
 */
static void ev_handler(struct mg_connection * nc,  int ev,  void * ev_data){
  static const struct mg_str api_prefix = MG_MK_STR("/messenger_api");
  struct http_message * hm = (http_message *) ev_data;
  
  switch (ev){
    case MG_EV_HTTP_REQUEST:
      if (has_prefix(&hm->uri, &api_prefix)){
        if (is_equal(&hm->method, &s_post_method)){
          db_op(nc, hm, s_db_handle, API_OP_POST);
        } else {
          mg_http_send_error(nc, 501, "Not implemented");
        }
      } else {
        mg_serve_http(nc, hm, s_http_server_opts);
      }
    default:
      break;
  }
}

/**
 * @brief Точка входа
 */
int main(int argc, char* argv[]) {
  /* Менеджер событий, который содержит все активные соединения */
  struct mg_mgr mgr;
  struct mg_connection *nc;

  /* Open listening socket */
  mg_mgr_init(&mgr, NULL);
  nc = mg_bind(&mgr, s_http_port, ev_handler);
  mg_set_protocol_http_websocket(nc);
  s_http_server_opts.document_root = "web_root";

  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  /* Open database */
  if ((s_db_handle = db_open(s_db_path)) == NULL) {
    fprintf(stderr, "Cannot open DB [%s]\n", s_db_path);
    exit(EXIT_FAILURE);
  }

  /* Run event loop until signal is received */
  printf("Starting RESTful server on port %s\n", s_http_port);
  while (s_sig_num == 0) {
    mg_mgr_poll(&mgr, 1000);
  }

  /* Cleanup */
  mg_mgr_free(&mgr);
  db_close(&s_db_handle);

  printf("Exiting on signal %d\n", s_sig_num);


  return 0;
}

