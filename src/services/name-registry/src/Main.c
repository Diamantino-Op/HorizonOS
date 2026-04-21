 #include <errno.h>
 #include <inttypes.h>
 #include <pthread.h>
 #include <stdbool.h>
 #include <stdint.h>
 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <unistd.h>

 #include "horizonos/generic.h"
 #include "abi-bits/hos_msg.h"

 #include "Service.h"

 #define MESSAGE_BUFFER_SIZE 1024

 typedef struct ServiceNode {
	 Service service;
	 struct ServiceNode *next;
 } ServiceNode;

 static pthread_mutex_t services_mutex = PTHREAD_MUTEX_INITIALIZER;
 static ServiceNode *services = NULL;

 static void *messageHandlerMain(void *arg);
 static bool registerService(uint64_t port, uint64_t ownerPid, uint64_t tid, const char *name, uint64_t versionMajor, uint64_t versionMinor, uint64_t versionPatch);
 static bool unregisterService(const char *name);
 static bool serviceExistsByNameAndTid(const char *name, uint64_t tid);
 static bool serviceExistsByName(const char *name, uint64_t *portOut);
 static void sendResponse(uint64_t srcPort, const char *response);
 static bool parseUint64(const char *text, uint64_t *valueOut);
 static char *duplicateString(const char *text);
 static void freeServiceNode(ServiceNode *node);

 int main(int argc, char **argv) {
	 (void)argc;
	 (void)argv;

	 pthread_t messageHandler;
	 const int createErr = pthread_create(&messageHandler, NULL, messageHandlerMain, NULL);

	 if (createErr != 0) {
		 printf("Name/Registry Service: Failed to start message handler thread: %d", createErr);
		 return 1;
	 }

	 pthread_detach(messageHandler);

	 while (true) {
		 pthread_mutex_lock(&services_mutex);

		 ServiceNode *node = services;
		 while (node != NULL) {
			 bool ret = false;
			 const int err = is_thread_alive(node->service.tid, &ret);

			 if (err == 0 && !ret) {
				 printf("Service: %s dead, unregistering it!", node->service.name);

				 ServiceNode *deadNode = node;
				 if (node == services) {
					 services = node->next;
					 node = services;
				 } else {
					 ServiceNode *previous = services;
					 while (previous != NULL && previous->next != deadNode) {
						 previous = previous->next;
					 }

					 if (previous != NULL) {
						 previous->next = deadNode->next;
						 node = previous->next;
					 } else {
						 node = deadNode->next;
					 }
				 }

				 printf("Service %s unregistered!", deadNode->service.name);
				 freeServiceNode(deadNode);
				 continue;
			 }

			 node = node->next;
		 }

		 pthread_mutex_unlock(&services_mutex);
		 usleep(100000);
	 }

	 return 0;
 }

 static void *messageHandlerMain(void *arg) {
	 (void)arg;

	 const int registerResult = register_horizonos_port(1);

	 if (registerResult == 0) {
		 printf("Name/Registry Service: Successfully registered port!");
	 } else {
		 printf("Name/Registry Service: Failed to register port: %d", registerResult);
		 return NULL;
	 }

	 while (true) {
		 char receiveBuffer[MESSAGE_BUFFER_SIZE] = {0};
		 struct hos_msg msg = {0};

		 msg.buffer = receiveBuffer;
		 msg.length = sizeof(receiveBuffer);

		 const int err = receive_horizonos_message(1, &msg);

		 if (err != 0) {
			 continue;
		 }

		 if (msg.ret_length < 0 || (size_t)msg.ret_length > sizeof(receiveBuffer)) {
			 printf("Name/Registry Service: Dropped oversized message (%ld bytes)\n", (long)msg.ret_length);
			 continue;
		 }

		 char message[MESSAGE_BUFFER_SIZE + 1];
		 memcpy(message, receiveBuffer, (size_t)msg.ret_length);
		 message[msg.ret_length] = '\0';

		 char *parts[7] = {0};
		 size_t partCount = 0;
		 char *cursor = message;

		 while (partCount < 7) {
			 parts[partCount++] = cursor;

			 char *separator = strchr(cursor, ';');
			 if (separator == NULL) {
				 break;
			 }

			 *separator = '\0';
			 cursor = separator + 1;
		 }

		 if (partCount == 0 || parts[0] == NULL || parts[0][0] == '\0') {
			 continue;
		 }

		 if (strcmp(parts[0], "register") == 0) {
			 if (partCount < 7) {
				 continue;
			 }

			 uint64_t ownerPid = 0;
			 uint64_t tid = 0;
			 uint64_t versionMajor = 0;
			 uint64_t versionMinor = 0;
			 uint64_t versionPatch = 0;

			 if (!parseUint64(parts[1], &ownerPid) || !parseUint64(parts[2], &tid) || !parseUint64(parts[4], &versionMajor) || !parseUint64(parts[5], &versionMinor) || !parseUint64(parts[6], &versionPatch)) {
				 continue;
			 }

			 pthread_mutex_lock(&services_mutex);
			 const bool inserted = registerService(msg.src_port, ownerPid, tid, parts[3], versionMajor, versionMinor, versionPatch);
			 pthread_mutex_unlock(&services_mutex);

			 char ret[2] = { inserted ? '1' : '0', '\0' };
			 sendResponse(msg.src_port, ret);
			 continue;
		 }

		 if (strcmp(parts[0], "unregister") == 0) {
			 if (partCount < 2) {
				 continue;
			 }

			 pthread_mutex_lock(&services_mutex);
			 (void)unregisterService(parts[1]);
			 pthread_mutex_unlock(&services_mutex);
			 continue;
		 }

		 if (strcmp(parts[0], "get") == 0) {
			 if (partCount < 2) {
				 continue;
			 }

			 uint64_t port = 0;

			 pthread_mutex_lock(&services_mutex);
			 (void)serviceExistsByName(parts[1], &port);
			 pthread_mutex_unlock(&services_mutex);

			 char ret[32];
			 const int retLen = snprintf(ret, sizeof(ret), "%" PRIu64, port);
			 if (retLen > 0 && (size_t)retLen < sizeof(ret)) {
				 sendResponse(msg.src_port, ret);
			 }

			 continue;
		 }

		 if (strcmp(parts[0], "check") == 0) {
			 if (partCount < 3) {
				 continue;
			 }

			 uint64_t tid = 0;
			 if (!parseUint64(parts[2], &tid)) {
				 continue;
			 }

			 pthread_mutex_lock(&services_mutex);
			 const bool exists = serviceExistsByNameAndTid(parts[1], tid);
			 pthread_mutex_unlock(&services_mutex);

			 char ret[2] = { exists ? '1' : '0', '\0' };
			 sendResponse(msg.src_port, ret);
			 continue;
		 }
	 }

	 return NULL;
 }

 static bool registerService(uint64_t port, uint64_t ownerPid, uint64_t tid, const char *name, uint64_t versionMajor, uint64_t versionMinor, uint64_t versionPatch) {
	 ServiceNode **cursor = &services;
	 while (*cursor != NULL) {
		 if (strcmp((*cursor)->service.name, name) == 0) {
			 return false;
		 }

		 cursor = &(*cursor)->next;
	 }

	 ServiceNode *node = malloc(sizeof(*node));
	 if (node == NULL) {
		 printf("Name/Registry Service: Failed to allocate service entry for %s", name);
		 return false;
	 }

	 node->service.port = port;
	 node->service.ownerPid = ownerPid;
	 node->service.tid = tid;
	 node->service.name = duplicateString(name);
	 node->service.versionMajor = versionMajor;
	 node->service.versionMinor = versionMinor;
	 node->service.versionPatch = versionPatch;
	 node->next = NULL;

	 if (node->service.name == NULL) {
		 free(node);
		 printf("Name/Registry Service: Failed to duplicate service name for %s", name);
		 return false;
	 }

	 *cursor = node;
	 printf("Service %s registered!", name);
	 return true;
 }

 static bool unregisterService(const char *name) {
	 ServiceNode **cursor = &services;
	 while (*cursor != NULL) {
		 if (strcmp((*cursor)->service.name, name) == 0) {
			 ServiceNode *node = *cursor;
			 *cursor = node->next;
			 printf("Service %s unregistered!", name);
			 freeServiceNode(node);
			 return true;
		 }

		 cursor = &(*cursor)->next;
	 }

	 return false;
 }

 static bool serviceExistsByName(const char *name, uint64_t *portOut) {
	 for (ServiceNode *node = services; node != NULL; node = node->next) {
		 if (strcmp(node->service.name, name) == 0) {
			 if (portOut != NULL) {
				 *portOut = node->service.port;
			 }

			 return true;
		 }
	 }

	 if (portOut != NULL) {
		 *portOut = 0;
	 }

	 return false;
 }

 static bool serviceExistsByNameAndTid(const char *name, uint64_t tid) {
	 for (ServiceNode *node = services; node != NULL; node = node->next) {
		 if (strcmp(node->service.name, name) == 0 && node->service.tid == tid) {
			 return true;
		 }
	 }

	 return false;
 }

 static void sendResponse(uint64_t srcPort, const char *response) {
	 struct hos_msg newMsg = {0};
	 newMsg.port = srcPort;
	 newMsg.buffer = (void *)response;
	 newMsg.length = strlen(response);
	 send_horizonos_message(srcPort, &newMsg);
 }

 static bool parseUint64(const char *text, uint64_t *valueOut) {
	 if (text == NULL || valueOut == NULL || text[0] == '\0' || text[0] == '-') {
		 return false;
	 }

	 errno = 0;
	 char *end = NULL;
	 const unsigned long long value = strtoull(text, &end, 10);

	 if (errno != 0 || end == text || *end != '\0') {
		 return false;
	 }

	 *valueOut = (uint64_t)value;
	 return true;
 }

 static char *duplicateString(const char *text) {
	 if (text == NULL) {
		 return NULL;
	 }

	 const size_t length = strlen(text) + 1;
	 char *copy = malloc(length);
	 if (copy == NULL) {
		 return NULL;
	 }

	 memcpy(copy, text, length);
	 return copy;
 }

 static void freeServiceNode(ServiceNode *node) {
	 if (node == NULL) {
		 return;
	 }

	 free(node->service.name);
	 free(node);
 }

