
#ifndef LLM_HANDLER_HPP
#define LLM_HANDLER_HPP

#include <iostream>
#include <string>
#include <vector>
#include <regex> 
#include <curl/curl.h>
#include <thread>
#include <chrono>
#include "json.hpp"

using json = nlohmann::json;

struct QuizQuestion {
    std::string q;
    std::vector<std::string> options;
    std::string a;
    json to_json_question_only() const {
        return {{"q", q}, {"options", options}};
    }
};

struct QuizResult {
    std::vector<QuizQuestion> questions;
    bool success = false;
    std::string error_message;
};

static size_t WriteCallback(char *contents, size_t size, size_t nmemb, void *userp) {
    ((std::string*)userp)->append(contents, size * nmemb);
    return size * nmemb;
}

class LLMHandler {
public:
    LLMHandler(const std::string& rollno) : roll_number(rollno) {
        llm_base_url = "http://192.168.50.142:25000";
    }

    bool reserve_llm(std::string& error_msg) {
        std::cout << "[LLM] Attempting to reserve LLM instance for roll number: " << roll_number << "..." << std::endl;
        CURL *curl = curl_easy_init();
        if (!curl) {
            error_msg = "Failed to initialize cURL";
            return false;
        }

        std::string readBuffer;
        std::string url = llm_base_url + "/reserve";
        json payload = {{"rollno", roll_number}};
        
        std::string post_data = payload.dump();

        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, "Accept: */*");

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "curl/7.88.1");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);

        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);

        if (res == CURLE_OK && http_code == 200) {
            std::cout << "[LLM] Reservation successful." << std::endl;
            return true;
        } else {
            error_msg = "LLM Reservation failed. HTTP Code: " + std::to_string(http_code) + ", Response: " + readBuffer;
            return false;
        }
    }

    QuizResult generate_quiz(const std::string& genre) {
        QuizResult result;
        CURL *curl = curl_easy_init();
        if (!curl) {
            result.error_message = "Failed to initialize cURL";
            return result;
        }

        std::string readBuffer;
        std::string url = llm_base_url + "/generate_quiz";
        
        json payload = {
            {"rollno", roll_number},
            {"messages", {
                {
                    {"role", "system"},
                    {"content", "You are a trivia generator. Given a genre, output exactly 10 multiple-choice questions. Each question must have 4 options (A, B, C, D) and one correct answer. Return ONLY valid JSON in the following format: {\"questions\":[{\"q\":\"...\",\"options\":[\"A) ...\",\"B) ...\",\"C) ...\",\"D) ...\"],\"a\":\"A\"}, ...]}"}
                },
                {
                    {"role", "user"},
                    {"content", "Generate 10 MCQ trivia questions about " + genre}
                }
            }},
            {"temperature", 0.0},
            {"max_tokens", 1200}
        };

        std::string post_data = payload.dump();
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, "Accept: */*");

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "curl/7.88.1");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);

        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);

        if (res == CURLE_OK && http_code == 200) {
            json llm_response = json::parse(readBuffer);
            std::string content_str = llm_response["choices"][0]["message"]["content"];

        
            try {
                
                json quiz_json = json::parse(content_str);
                for (const auto& item : quiz_json["questions"]) {
                    result.questions.push_back({
                        item["q"],
                        item["options"].get<std::vector<std::string>>(),
                        item["a"]
                    });
                }
            } catch (json::parse_error& e) {
                
                std::cerr << "[WARN] LLM did not return valid JSON. Falling back to text parsing. Error: " << e.what() << std::endl;
                result.questions = parse_quiz_from_text(content_str);
            }
            

            if (!result.questions.empty()) {
                result.success = true;
            } else {
                result.error_message = "Failed to parse any questions from LLM response.";
            }
        } else {
            result.error_message = "HTTP Request Failed. Code: " + std::to_string(http_code) + ". Response: " + readBuffer;
        }
        return result;
    }

private:
    std::string roll_number;
    std::string llm_base_url;


    std::vector<QuizQuestion> parse_quiz_from_text(const std::string& content) {
        std::vector<QuizQuestion> questions;
        
        std::regex qa_regex(R"(\d+\.\s*(.*?)\s*\nA\)\s*(.*?)\s*\nB\)\s*(.*?)\s*\nC\)\s*(.*?)\s*\nD\)\s*(.*?)\s*\n\s*Answer:\s*([A-D]))");
        
        auto matches_begin = std::sregex_iterator(content.begin(), content.end(), qa_regex);
        auto matches_end = std::sregex_iterator();

        for (auto i = matches_begin; i != matches_end; ++i) {
            std::smatch match = *i;
            if (match.size() == 7) {
                
                std::string q_text = match[1].str();
                std::vector<std::string> options = {
                    "A) " + match[2].str(),
                    "B) " + match[3].str(),
                    "C) " + match[4].str(),
                    "D) " + match[5].str()
                };
                std::string answer = match[6].str();
                questions.push_back({q_text, options, answer});
            }
        }
        return questions;
    }
};

#endif 