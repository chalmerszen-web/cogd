#ifndef COGD_COMMUNICATION_MODE_H
#define COGD_COMMUNICATION_MODE_H
#define COGD_COMM_WSS 1
#define COGD_COMM_AOQ 2
#ifndef COGD_COMM_MODE
#define COGD_COMM_MODE COGD_COMM_WSS
#endif
#if COGD_COMM_MODE != COGD_COMM_WSS && COGD_COMM_MODE != COGD_COMM_AOQ
#error "Unsupported COGD communication mode"
#endif
#if COGD_COMM_MODE == COGD_COMM_AOQ && !AGENT_BOARD_P4
#error "Native AOQ currently requires the ESP32-P4 profile; C3 retains WSS"
#endif
#endif
