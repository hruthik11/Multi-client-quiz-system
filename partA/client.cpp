
#include <iostream>
#include <string>
#include <thread>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <vector>
#include <atomic>
#include "protocol.h"
#include "json.hpp"

using json = nlohmann::json;


std::atomic<bool> quiz_active = true;

bool send_message(int sock, MessageType type, const std::string& payload = "") {
    if (!quiz_active) return false;
    MessageHeader header;
    header.type = type;
    header.payload_size = htonl(payload.size());

    if (send(sock, &header, sizeof(header), 0) < 0) return false;
    if (!payload.empty()) {
        if (send(sock, payload.c_str(), payload.size(), 0) < 0) return false;
    }
    return true;
}


void receive_messages(int sock) {
    while (quiz_active) {
        MessageHeader header;
        int bytes_read = recv(sock, &header, sizeof(header), 0);
        if (bytes_read <= 0) {
            if (quiz_active) {
                std::cout << "\n[INFO] Server disconnected." << std::endl;
                std::cout << "Press Enter to exit." << std::endl;
            }
            quiz_active = false;
            break;
        }

        uint32_t payload_size = ntohl(header.payload_size);
        std::string payload;
        if (payload_size > 0) {
            std::vector<char> buffer(payload_size);
            if (recv(sock, buffer.data(), payload_size, 0) > 0) {
                 payload.assign(buffer.begin(), buffer.end());
            }
        }

        switch (header.type) {
            case MessageType::MSG_QUESTION: {
                json q = json::parse(payload);
                std::cout << "\n------------------------\n";
                std::cout << "Question: " << q["q"].get<std::string>() << std::endl;
                for (const auto& opt : q["options"]) {
                    std::cout << opt.get<std::string>() << std::endl;
                }
                std::cout << "------------------------\n";
                std::cout << "Your answer (A, B, C, D) or type 'leaderboard': ";
                std::cout.flush();
                break;
            }
            case MessageType::MSG_FEEDBACK: {
                json feedback = json::parse(payload);
                std::cout << "\n>>> Result: " << feedback["result"].get<std::string>()
                          << " | Your Score: " << feedback["score"].get<int>() << std::endl;
                std::cout << "\nWaiting for the next question..." << std::endl;
                break;
            }
            case MessageType::MSG_QUIZ_OVER: {
                std::cout << "\n====================\n"
                          << "   QUIZ OVER!\n"
                          << " Your final score is: " << payload << "\n"
                          << "====================\n"
                          << "Thank you for playing! Press Enter to exit." << std::endl;
                quiz_active = false;
                break;
            }
            
            case MessageType::MSG_LEADERBOARD_RESP: {
                json leaderboard = json::parse(payload);
                std::cout << "\n--- Leaderboard ---\n";
                for (const auto& entry : leaderboard) {
                    std::cout << entry["name"].get<std::string>()
                              << ": " << entry["score"].get<int>() << std::endl;
                }
                std::cout << "-------------------\n";
                std::cout << "Your answer (A, B, C, D) or type 'leaderboard': ";
                std::cout.flush();
                break;
            }
            
            case MessageType::MSG_KEEP_ALIVE: {
                send_message(sock, MessageType::MSG_ALIVE_OK);
                break;
            }
            case MessageType::MSG_ERROR: {
                std::cerr << "\n[SERVER ERROR] " << payload << std::endl;
                std::cerr << "Exiting." << std::endl;
                quiz_active = false;
                break;
            }
            default:
                break;
        }
    }
}

int main(int argc, char const* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <server_ip> <server_port>" << std::endl;
        return -1;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { std::cerr << "Socket creation error\n"; return -1; }

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(std::stoi(argv[2]));
    if (inet_pton(AF_INET, argv[1], &serv_addr.sin_addr) <= 0) {
        std::cerr << "Invalid address/ Address not supported \n";
        return -1;
    }

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "Connection Failed to " << argv[1] << ":" << argv[2] << std::endl; return -1;
    }

    std::cout << "Connected to the quiz server!" << std::endl;
    std::cout << "Please enter a quiz genre (e.g., science, history, movies): ";
    std::string genre;
    std::getline(std::cin, genre);
    send_message(sock, MessageType::MSG_GENRE, genre);

    std::thread receiver_thread(receive_messages, sock);

    std::string user_input;
    while (quiz_active && std::getline(std::cin, user_input)) {
        if (!quiz_active) break;

        if (user_input == "leaderboard") {
            send_message(sock, MessageType::MSG_LEADERBOARD_REQ);
        } else if (user_input.length() == 1 && isalpha(user_input[0])) {
            json answer_payload = {
                {"answer", std::string(1, toupper(user_input[0]))}
            };
            send_message(sock, MessageType::MSG_ANSWER, answer_payload.dump());
        } else if (quiz_active) {
            std::cout << "Invalid input. Please enter a single letter (A-D) for an answer, or type 'leaderboard'." << std::endl;
            std::cout << "Your answer (A, B, C, D) or type 'leaderboard': ";
            std::cout.flush();
        }
    }

    receiver_thread.join();
    close(sock);
    return 0;
}