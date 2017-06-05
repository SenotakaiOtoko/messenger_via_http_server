/**
 * @file 
 * @brief Прослойка между SQLite и mongoose
 *
 * Функции, содержащиеся в файле выполняют роль прослойки между базой данных 
 * SQLite и серверной частью приложения, написанной с использованием mongoose 
 * networking library. Так же содержит все функции api сервера.
 *
 */

#include <string.h>
#include <sstream>

#include "mongoose.h"
#include "db_plugin.h"
#include "sqlite3.h"

extern int is_equal(const struct mg_str * s1, const struct mg_str * s2);

/**
 * @brief Функция открывает локальную базу данных, а если она не существует, то создаёт
 * новую
 *
 * @param[in] db_path Путь к базе данных
 * @return Указатель на handler базы данных
 */
void * db_open(const char * db_path) {
  sqlite3 * db = NULL;
  if (sqlite3_open_v2(db_path, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                                        SQLITE_OPEN_FULLMUTEX, 
      NULL) == SQLITE_OK){
  // Create messages table
  sqlite3_exec(db, "CREATE TABLE IF NOT EXIST \"messages\" ( "
    "\"message_id\" INTEGER PRIMARY KEY AUTOINCREMENT UNIQUE, "
    "\"from\" TEXT, "
    "\"to\" TEXT, "
    "\"message\" TEXT, "
    "\"date\" INTEGER )",
    0, 0, 0);
  // Create users table
  sqlite3_exec(db, "CREATE TABLE IF NOT EXIST \"users\" ( "
    "\"user\" TEXT UNIQUE, "
    "\"pass_hash\" TEXT, "
    "PRIMARY KEY(\"user\") )",
    0, 0, 0);
  }
  return db;
}


/**
 * @brief Функция закрывает базу данных
 *
 * @param[in] db_handle указатель на handler базы данных, которую необходимо 
 * закрыть
 */
void db_close(void ** db_handle) {
  if (db_handle != NULL && *db_handle != NULL) {
    sqlite3_close((sqlite3 *)*db_handle);
    *db_handle = NULL;
  }
}


/**
 * @brief Функция формирует строку - JSON сообщение
 *
 * @param[in] message_id Уникальный идентификатор сообщения
 * @param[in] from От кого адресовано сообщение
 * @param[in] to Кому адресовано сообщение
 * @param[in] message Текст сообщения
 * @param[in] time Время, в которое сообщение было получено сервером (UTC Unix)
 * @return Указатель на строку, содержащую JSON сообщение
 */
char * build_message_json(const char * message_id, 
                          const char * from, 
                          const char * to, 
                          const char * message, 
                          const char * time){
                            
  char * result = new char[strlen(message_id) +
                           strlen(from) +
                           strlen(to) +
                           strlen(message) +
                           strlen(time) + 60];
  *result = NULL;
  strcat(result, "{");
  strcat(result, "\"message_id\":");
  strcat(result, message_id);
  strcat(result, ",\"from\":\"");
  strcat(result, from);
  strcat(result, "\",\"to\":\"");
  strcat(result, to);
  strcat(result, "\",\"message\":\"");
  strcat(result, message);
  strcat(result, "\",\"time\":");
  strcat(result, time);
  strcat(result, "}");
  return result;
}


/**
 * @brief Функция парсит параметр action HTTP запроса
 *
 * @param[in] buf Тело HTTP запроса с параметрами
 * @return enum api_action
 */
int switch_action(const mg_str * buf){
  char action[40];
  int result = mg_get_http_var(buf, "action", action, sizeof(action));
  if (result == 0 || result == -1){
    return API_ACTION_NULL;
  }
  if (!strcmp(action, "send_message")){
    return API_ACTION_SEND_MESSAGE;
  }
  if (!strcmp(action, "get_message")){
    return API_ACTION_GET_MESSAGE;
  }
  if (!strcmp(action, "get_user")){
    return API_ACTION_GET_USER;
  }
  if (!strcmp(action, "register")){
    return API_ACTION_REGISTER;
  }
  return API_ACTION_NULL;
}


/**
 * @brief Функция выполняет проверку авторизации
 *
 * @todo Переписать функцию, используя функцию get_user_from_db
 *
 * @param[in] hm Тело HTTP запроса
 * @param[in] db Handler базы данных
 * @retval NULL если пользователь не найден, или неправильный пароль
 * @retval Указатель на строку, содержащую имя пользователя
 */
char * check_auth(const http_message * hm, 
                  void * db){
  // Vars
  sqlite3_stmt * stmt = NULL;
  char * user = new char[USERNAME_MAX_LENGTH];
  char   pass[PASS_MAX_LENGTH];
  const char * pass_db = NULL;

  if(mg_get_http_basic_auth(
     (http_message *)hm, user, USERNAME_MAX_LENGTH, pass, sizeof(pass)
     ) != 0){
    sqlite3_finalize(stmt);
    delete[] user;
    return NULL;
  }

  if (sqlite3_prepare_v2((sqlite3 *)db, "SELECT \"pass_hash\" FROM \"users\" "
  "WHERE \"user\" = ?;", -1, &stmt, NULL) != SQLITE_OK){
    sqlite3_finalize(stmt);
    delete[] user;
    return NULL;
  }
  sqlite3_bind_text(stmt, 1, user, strlen(user), SQLITE_STATIC);
  int result = sqlite3_step(stmt);
  pass_db = (char*)sqlite3_column_text(stmt, 0);
  if ((result != SQLITE_ROW && result != SQLITE_DONE) || 
       pass_db == NULL || strcmp(pass, pass_db)){
    sqlite3_finalize(stmt);
    delete[] user;
    return NULL;
  }

  sqlite3_finalize(stmt);
  return user;
}


/**
 * @brief Функция api получения сообщения
 *
 * Функция проверяет авторизацию, достаёт из базы данных первое сообщение, 
 * которое неизвестно клиенту и отправляет ответ. В случае, если сообщение не 
 * найдено, возвращает ответ об отвутствии новых сообщений.
 *
 * @param[in] nc Соединение, по которому нужно отправить ответ
 * @param[in] hm Тело HTTP запроса
 * @param[in] db Handler базы данных
 */
void get_message(struct mg_connection * nc, 
                 const struct http_message * hm,
                 void * db){
              
  sqlite3_stmt * stmt = NULL;
  char * user = check_auth(hm, db);
  
  if (user == NULL){
    mg_http_send_error(nc, 401, "Unauthorized");
    return;
  }
  
  const struct mg_str *body =
      hm->query_string.len > 0 ? &hm->query_string : &hm->body;

  char * last_message = new char[24];
  
  int result = mg_get_http_var(body, "last_message", last_message, sizeof(last_message));

  int64_t last_message_i;
  
  if (result < 1){
    last_message_i = 0;
  } else {
    last_message_i = atoi(last_message);
  }

  if (sqlite3_prepare_v2((sqlite3 *)db, "SELECT \"message_id\", \"from\", \"to\", \"message\", \"date\" FROM \"messages\" "
  "WHERE (\"from\" = ? OR \"to\" = ?) AND \"message_id\" > ?;", -1, &stmt, NULL) != SQLITE_OK){
    mg_http_send_error(nc, 500, "Internal server error");
    delete[] user;
    delete[] last_message;
    return;
  }
  sqlite3_bind_text(stmt, 1, user, strlen(user), SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, user, strlen(user), SQLITE_STATIC);
  sqlite3_bind_int64(stmt, 3, last_message_i);
  result = sqlite3_step(stmt);
  if (result != SQLITE_ROW){
    mg_http_send_error(nc, 204, "No content");
    delete[] user;
    delete[] last_message;
    return;
  }
  
  char * answer =
    build_message_json((char*)sqlite3_column_text(stmt, 0), 
                       (char*)sqlite3_column_text(stmt, 1), 
                       (char*)sqlite3_column_text(stmt, 2), 
                       (char*)sqlite3_column_text(stmt, 3), 
                       (char*)sqlite3_column_text(stmt, 4));
  
  mg_printf(nc,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: text/json\r\n"
              "Content-Length: %d\r\n\r\n%s",
              strlen(answer), answer);
#ifdef _DEBUG
  printf("%s get message with id %s\n", user, (char*)sqlite3_column_text(stmt, 0));
#endif

  
  sqlite3_finalize(stmt);

  delete[] user;
  delete[] last_message;
  delete[] answer;
  
}


/**
 * @brief Функция api отправки сообщения
 *
 * Функция проверяет авторизацию, правильность запроса, кладёт в базу данных 
 * новое сообщение.
 *
 * @param[in] nc Соединение, по которому нужно отправить ответ
 * @param[in] hm Тело HTTP запроса
 * @param[in] db Handler базы данных
 */
void send_message(struct mg_connection * nc, 
                  const struct http_message * hm,
                  void * db){
              
  sqlite3_stmt * stmt = NULL;
  const struct mg_str *body =
      hm->query_string.len > 0 ? &hm->query_string : &hm->body;

  char * user = check_auth(hm, db);
  
  if (user == NULL){
    mg_http_send_error(nc, 401, "Unauthorized");
    return;
  }

  char message[4096];
  
  
  
  char * to = new char[USERNAME_MAX_LENGTH];

  int result = mg_get_http_var(body, "to", to, USERNAME_MAX_LENGTH);

  if (result < 1 ||
    !mg_get_http_var(body, "message", message, sizeof(message))){
    mg_http_send_error(nc, 400, "Bad request");
    delete[] to;
    delete[] user;
    return;
  }
  if (sqlite3_prepare_v2((sqlite3 *)db, "INSERT INTO \"messages\" "
           "VALUES (?, ?, ?, ?, ?);", -1, &stmt, NULL) != SQLITE_OK){
    mg_http_send_error(nc, 500, "Internal server error");
    sqlite3_finalize(stmt);
    delete[] to;
    delete[] user;
    return;
  }
  sqlite3_bind_text(stmt,  2, user,    strlen(user),    SQLITE_STATIC);
  sqlite3_bind_text(stmt,  3, to,      strlen(to),      SQLITE_STATIC);
  sqlite3_bind_text(stmt,  4, message, strlen(message), SQLITE_STATIC);
  sqlite3_bind_int64(stmt, 5, time(NULL));
  
  if (sqlite3_step(stmt) != SQLITE_DONE){ // TODO: ¬ы¤снить, почему этот метод работает так долго
    mg_http_send_error(nc, 500, "Internal server error");
    sqlite3_finalize(stmt);
    delete[] to;
    delete[] user;
    return;
  }
  mg_printf(nc,
              "HTTP/1.1 200 OK\r\n"
              "Content-Length: 0\r\n\r\n");
  sqlite3_finalize(stmt);
#ifdef _DEBUG
  printf("%s sent message to %s\n", user, to);
#endif
  delete[] to;
  delete[] user;
}


/**
 * @brief Функция достаёт данные о пользователе из базы данных
 *
 * @param[in] db Handler базы данных
 * @param[in] user Указатель на строку, содержащую имя пользователя
 * @retval NULL если пользователь не найден в базе данных
 * @retval Указатель на строку, содержащую имя пользователя
 */
char * get_user_from_db(void * db, 
                        char * user){
    // Vars
  sqlite3_stmt * stmt = NULL;

  if (sqlite3_prepare_v2((sqlite3 *)db, "SELECT \"user\" FROM \"users\" "
  "WHERE \"user\" = ?;", -1, &stmt, NULL) != SQLITE_OK){
    sqlite3_finalize(stmt);
    return NULL;
  }
  sqlite3_bind_text(stmt, 1, user, strlen(user), SQLITE_STATIC);
  int result = sqlite3_step(stmt);
  if ((result != SQLITE_ROW && result != SQLITE_DONE) || 
       (char*)sqlite3_column_text(stmt, 0) == NULL){
    sqlite3_finalize(stmt);
    return NULL;
  }

  char * result_user = new char[USERNAME_MAX_LENGTH];
  strcpy(result_user, user);

  sqlite3_finalize(stmt);
  return result_user;
}


/**
 * @brief Функция api регистрации нового пользователя
 *
 * Функция проверяет правильность запроса, пытается добавить пользователя в базу
 * данных. Результат попытки отправляет по открытому соединению.
 *
 * @param[in] nc Соединение, по которому нужно отправить ответ
 * @param[in] hm Тело HTTP запроса
 * @param[in] db Handler базы данных
 */
void register_user(struct mg_connection * nc, 
                   const struct http_message * hm,
                   void * db){
  
  sqlite3_stmt * stmt = NULL;

  const struct mg_str *body =
      hm->query_string.len > 0 ? &hm->query_string : &hm->body;

  char * user = new char[USERNAME_MAX_LENGTH];
  int result = mg_get_http_var(body, "user", user, USERNAME_MAX_LENGTH);
  if (result < 1){
    mg_http_send_error(nc, 400, "Bad request");
    delete[] user;
    return;
  }
  
  char pass[256];
  result = mg_get_http_var(body, "password", pass, sizeof(pass));
  if (result < 1){
    mg_http_send_error(nc, 400, "Bad request");
    delete[] user;
    return;
  }

  if (sqlite3_prepare_v2((sqlite3 *)db, "INSERT INTO \"users\" VALUES (?, ?);", -1,
                       &stmt, NULL) != SQLITE_OK) {
    mg_http_send_error(nc, 500, "Internal server error");
    delete[] user;
    sqlite3_finalize(stmt);
    return;
  }
  sqlite3_bind_text(stmt, 1, user, strlen(user), SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, pass, strlen(pass), SQLITE_STATIC);
  result = sqlite3_step(stmt);
  if (result != SQLITE_DONE){
    mg_http_send_error(nc, 401, "User already exist");
  } else {
#ifdef _DEBUG
    printf("%s registered\n", user);
#endif
    mg_printf(nc,
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 23\r\n\r\n"
                "Registration successful");
  }
  delete[] user;
  sqlite3_finalize(stmt);

}


/**
 * @brief Функция api получения данных о пользователе
 *
 * Функция проверяет правильность запроса, отправляет по соединению данные о 
 * пользователе.
 *
 * @param[in] nc Соединение, по которому нужно отправить ответ
 * @param[in] hm Тело HTTP запроса
 * @param[in] db Handler базы данных
 */
void get_user(struct mg_connection * nc, 
              const struct http_message * hm,
              void * db){
  sqlite3_stmt * stmt = NULL;

  const struct mg_str *body =
      hm->query_string.len > 0 ? &hm->query_string : &hm->body;

  char * user = new char[USERNAME_MAX_LENGTH];
  int result = mg_get_http_var(body, "user", user, USERNAME_MAX_LENGTH);
  if (result < 1){
    mg_http_send_error(nc, 400, "Bad request");
    delete[] user;
    return;
  }

  char * user_db = get_user_from_db(db, user);
  if (user_db == NULL){
    mg_http_send_error(nc, 404, "Not found");
    delete[] user;
    return;
  }
  
    mg_printf(nc,
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: %d\r\n\r\n"
                "%s", strlen(user_db), user_db);

  delete[] user_db;
  delete[] user;
}


/**
 * @brief Функция-обработчик POST запроса к api
 *
 * @param[in] nc Соединение, по которому нужно отправить ответ
 * @param[in] hm Тело HTTP запроса
 * @param[in] db Handler базы данных
 */
void op_post(struct mg_connection * nc, 
             const struct http_message * hm,
             void * db){

  const struct mg_str *body =
      hm->query_string.len > 0 ? &hm->query_string : &hm->body;

  int action = switch_action(body);

  switch (action)
  {
  case API_ACTION_GET_MESSAGE:
    get_message(nc, hm, db);
    break;
  case API_ACTION_SEND_MESSAGE:
    send_message(nc, hm, db);
    break;
  case API_ACTION_GET_USER:
    get_user(nc, hm, db);
    break;
  case API_ACTION_REGISTER:
    register_user(nc, hm, db);
    break;
  default:
    mg_http_send_error(nc, 501, "Not implemented");
    break;
  }
}


/**
 * @brief Функция-обработчик любого запроса к api
 *
 * @param[in] nc Соединение, по которому нужно отправить ответ
 * @param[in] hm Тело HTTP запроса
 * @param[in] db Handler базы данных
 * @param[in] op Тип запроса к api
 */
void db_op(struct mg_connection *nc, 
           const struct http_message *hm,
           void *db, 
           int op){
  switch (op) {
    case API_OP_POST:
      op_post(nc, hm, db);
      break;
    default:
      mg_http_send_error(nc, 501, "Not implemented");
      break;
  }
}

