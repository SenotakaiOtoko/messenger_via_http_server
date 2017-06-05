/**
 * @file 
 * @brief Заголовочный файл, содержащий константы для прослойки и объявления 
 * некоторых функций.
 *
 */

#ifndef _MESSENGER_VIA_HTTP_SERVER__DB_PLUGIN_H_
#define _MESSENGER_VIA_HTTP_SERVER__DB_PLUGIN_H_

#include "sqlite3.h"

/// Максимальная длина имени пользователя
#define USERNAME_MAX_LENGTH 40

/// Максимальная длина пароля
#define PASS_MAX_LENGTH 256

/// Набор возможных типов запросов к api
enum api_op { 
  API_OP_POST, ///< POST
  API_OP_GET, ///< GET
  API_OP_SET, ///< SET
  API_OP_DEL  ///< DELETE
};

/// Набор возможных запросов-действий к api
enum api_action { 
  API_ACTION_NULL, ///< Указывает, что действие неизвестно api
  API_ACTION_SEND_MESSAGE, ///< Отправка сообщения
  API_ACTION_GET_MESSAGE, ///< Получение сообщения
  API_ACTION_REGISTER, ///< Регистрация нового пользователя
  API_ACTION_GET_USER ///< Получение данных о пользователе
};

void * db_open(const char * db_path);


void db_close(void ** db_handle);


char * build_message_json(const char * message_id, 
                          const char * from, 
                          const char * to, 
                          const char * message, 
                          const char * time);

                          
int switch_action(const mg_str * buf);                          


char * check_auth(const http_message * hm, 
                  void * db);


void get_message(struct mg_connection * nc, 
                 const struct http_message * hm,
                 void * db);
                 
                 
void send_message(struct mg_connection * nc, 
                  const struct http_message * hm,
                  void * db);

                  
char * get_user_from_db(void * db, 
                    char * user);


void register_user(struct mg_connection * nc, 
                   const struct http_message * hm,
                   void * db);

                   
void get_user(struct mg_connection * nc, 
              const struct http_message * hm,
              void * db);

            
void op_post(struct mg_connection * nc, 
             const struct http_message * hm,
             void * db);

             
void db_op(struct mg_connection *nc, 
           const struct http_message *hm,
           void *db, int op);             

           
void db_op(struct mg_connection *nc, 
           const struct http_message *hm, 
           void *db, 
           int op);

           
#endif //_MESSENGER_VIA_HTTP_SERVER__DB_PLUGIN_H_