// protocol.h
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstdint>


enum class MessageType : uint8_t {
    MSG_GENRE,
    MSG_QUESTION,
    MSG_ANSWER,
    MSG_FEEDBACK,
    MSG_LEADERBOARD_REQ,
    MSG_LEADERBOARD_RESP,
    MSG_QUIZ_OVER,
    MSG_ERROR,
    MSG_KEEP_ALIVE,
    MSG_ALIVE_OK
};


#pragma pack(push, 1) 
struct MessageHeader {
    MessageType type;
    uint32_t payload_size;
};
#pragma pack(pop)

#endif 