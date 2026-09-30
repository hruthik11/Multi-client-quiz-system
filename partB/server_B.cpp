
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <map>
#include <chrono>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <atomic>
#include <utility>
#include <tuple>
#include "protocol.h"
#include "llm_handler.hpp"

#define SERVER_PORT 1143
#define MAX_CLIENTS 10
#define KEEP_ALIVE_TIMEOUT 15 
#define CACHE_EXPIRATION_SECONDS 300 


struct ClientSession {
    int id, socket, score = 0, current_question_idx = 0;
    std::string name;
    std::vector<QuizQuestion> questions;
    std::atomic<time_t> last_message_time;

    
    ClientSession(int p_id, int p_socket, const std::string& p_name)
        : id(p_id), socket(p_socket), score(0), current_question_idx(0), 
          name(p_name), last_message_time(time(nullptr)) {}
          
    ClientSession(const ClientSession&) = delete;
    ClientSession& operator=(const ClientSession&) = delete;

    
    ClientSession(ClientSession&& other) noexcept
        : id(other.id), socket(other.socket), score(other.score), 
          current_question_idx(other.current_question_idx), name(std::move(other.name)),
          questions(std::move(other.questions)) {
        last_message_time.store(other.last_message_time.load());
    }
};


struct CacheEntry {
    std::vector<QuizQuestion> questions;
    std::chrono::steady_clock::time_point timestamp;
};


std::map<int, ClientSession> active_clients;
std::mutex clients_mutex;

std::map<std::string, CacheEntry> quiz_cache;
std::mutex cache_mutex;
const std::string YOUR_ROLL_NO = "cs23btech11043";


bool send_message(int sock, MessageType type, const std::string& payload = "") {
    MessageHeader header = {type, htonl(payload.size())};
    if (send(sock, &header, sizeof(header), 0) < 0) return false;
    if (!payload.empty() && send(sock, payload.c_str(), payload.size(), 0) < 0) return false;
    return true;
}

void handle_client(int client_socket, int client_id, LLMHandler& llm) {
    sockaddr_in address;
    socklen_t addrlen = sizeof(address);
    getpeername(client_socket, (sockaddr*)&address, &addrlen);
    std::cout << "[INFO] Client " << client_id << " connected from "
              << inet_ntoa(address.sin_addr) << ":" << ntohs(address.sin_port) << std::endl;

    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        active_clients.emplace(std::piecewise_construct,
                               std::forward_as_tuple(client_id),
                               std::forward_as_tuple(client_id, client_socket, "Client " + std::to_string(client_id)));
    }

    bool client_active = true;
    while (client_active) {
        MessageHeader header;
        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
        
        int bytes_read = recv(client_socket, &header, sizeof(header), 0);

        if (bytes_read <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            std::cout << "[INFO] Client " << client_id << " disconnected." << std::endl;
            break;
        }

        active_clients.at(client_id).last_message_time.store(time(nullptr));

        std::string payload_str;
        uint32_t payload_size = ntohl(header.payload_size);
        if (payload_size > 0) {
            std::vector<char> payload_buf(payload_size);
            recv(client_socket, payload_buf.data(), payload_size, 0);
            payload_str.assign(payload_buf.begin(), payload_buf.end());
        }

        switch (header.type) {
            case MessageType::MSG_GENRE: {
                std::cout << "[INFO] Client " << client_id << " requested genre: " << payload_str << std::endl;
                std::vector<QuizQuestion> questions_to_send;
                bool from_cache = false;

                {
                    std::lock_guard<std::mutex> lock(cache_mutex);
                    auto it = quiz_cache.find(payload_str);
                    if (it != quiz_cache.end()) {
                        auto age = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - it->second.timestamp);
                        if (age.count() < CACHE_EXPIRATION_SECONDS) {
                            questions_to_send = it->second.questions;
                            from_cache = true;
                            std::cout << "[CACHE] Hit for genre '" << payload_str << "'. Serving from cache." << std::endl;
                        } else {
                            quiz_cache.erase(it);
                            std::cout << "[CACHE] Stale entry for genre '" << payload_str << "' expired. Removing." << std::endl;
                        }
                    }
                }

                if (!from_cache) {
                    std::cout << "[CACHE] Miss for genre '" << payload_str << "'. Fetching from LLM." << std::endl;
                    QuizResult result = llm.generate_quiz(payload_str);
                    if (result.success && !result.questions.empty()) {
                        questions_to_send = result.questions;
                        {
                            std::lock_guard<std::mutex> lock(cache_mutex);
                            quiz_cache[payload_str] = {questions_to_send, std::chrono::steady_clock::now()};
                        }
                    } else {
                        std::cerr << "[ERROR] Client " << client_id << ": " << result.error_message << std::endl;
                        send_message(client_socket, MessageType::MSG_ERROR, "Failed to get questions from LLM.");
                        client_active = false;
                        break; 
                    }
                }

                if (!questions_to_send.empty()) {
                    std::lock_guard<std::mutex> lock(clients_mutex);
                    auto& client = active_clients.at(client_id);
                    client.questions = questions_to_send;
                    client.current_question_idx = 0;
                    
                    json question_payload = client.questions[0].to_json_question_only();
                    question_payload["cached"] = from_cache;
                    
                    send_message(client_socket, MessageType::MSG_QUESTION, question_payload.dump());
                }
                break;
            }
            case MessageType::MSG_ANSWER: {
                json answer_data = json::parse(payload_str);
                std::string answer = answer_data["answer"];
                std::lock_guard<std::mutex> lock(clients_mutex);
                auto& client = active_clients.at(client_id);
                if (static_cast<size_t>(client.current_question_idx) < client.questions.size()) {
                    bool is_correct = (client.questions[client.current_question_idx].a.rfind(answer, 0) == 0);
                    if (is_correct) client.score += 10;
                    json feedback = {{"result", is_correct ? "CORRECT" : "WRONG"}, {"score", client.score}};
                    send_message(client_socket, MessageType::MSG_FEEDBACK, feedback.dump());
                    client.current_question_idx++;
                    if (static_cast<size_t>(client.current_question_idx) < client.questions.size()) {
                        json question_payload = client.questions[client.current_question_idx].to_json_question_only();
                        question_payload["cached"] = true;
                        send_message(client_socket, MessageType::MSG_QUESTION, question_payload.dump());
                    } else {
                        send_message(client_socket, MessageType::MSG_QUIZ_OVER, std::to_string(client.score));
                        client_active = false;
                    }
                }
                break;
            }
            case MessageType::MSG_LEADERBOARD_REQ: {
                std::cout << "[INFO] Client " << client_id << " requested leaderboard." << std::endl;
                json leaderboard = json::array();
                std::lock_guard<std::mutex> lock(clients_mutex);
                for(const auto& pair : active_clients) {
                    leaderboard.push_back({
                        {"name", pair.second.name},
                        {"score", pair.second.score}
                    });
                }
                send_message(client_socket, MessageType::MSG_LEADERBOARD_RESP, leaderboard.dump());
                break;
            }
            case MessageType::MSG_ALIVE_OK: {
                break;
            }
            default:
                std::cerr << "[WARN] Client " << client_id << " sent unknown message type." << std::endl;
                break;
        }
    }

    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        active_clients.erase(client_id);
    }
    close(client_socket);
    std::cout << "[INFO] Cleaned up session for client " << client_id << "." << std::endl;
}

void keep_alive_checker() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        std::vector<int> clients_to_probe;
        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            time_t now = time(nullptr);
            for (auto const& [id, session] : active_clients) {
                if (difftime(now, session.last_message_time.load()) > KEEP_ALIVE_TIMEOUT) {
                    clients_to_probe.push_back(id);
                }
            }
        }
        
        for (int id : clients_to_probe) {
            std::lock_guard<std::mutex> lock(clients_mutex);
            auto it = active_clients.find(id);
            if (it != active_clients.end()) {
                 std::cout << "[KEEP-ALIVE] Sending keep-alive probe to client " << id << std::endl;
                 send_message(it->second.socket, MessageType::MSG_KEEP_ALIVE);
            }
        }
    }
}

int main() {
    LLMHandler llm(YOUR_ROLL_NO);
    std::string llm_error;
    if (!llm.reserve_llm(llm_error)) {
        std::cerr << "[FATAL] Could not reserve LLM instance. Server shutting down. Reason: " << llm_error << std::endl;
        exit(EXIT_FAILURE);
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) { perror("socket failed"); exit(EXIT_FAILURE); }
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(SERVER_PORT);

    if (bind(server_fd, (sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind failed"); exit(EXIT_FAILURE);
    }

    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("listen"); exit(EXIT_FAILURE);
    }

    std::cout << "[INFO] Server listening on port " << SERVER_PORT << std::endl;

    std::thread(keep_alive_checker).detach();

    int client_id_counter = 0;
    while (true) {
        socklen_t addrlen = sizeof(address);
        int new_socket = accept(server_fd, (sockaddr*)&address, &addrlen);
        if (new_socket < 0) { perror("accept"); continue; }

        if (active_clients.size() >= MAX_CLIENTS) {
            std::cout << "[WARN] Max clients reached. Rejecting new connection." << std::endl;
            send_message(new_socket, MessageType::MSG_ERROR, "Server is full. Please try again later.");
            close(new_socket);
        } else {
            std::thread(handle_client, new_socket, client_id_counter++, std::ref(llm)).detach();
        }
    }

    close(server_fd);
    return 0;
}