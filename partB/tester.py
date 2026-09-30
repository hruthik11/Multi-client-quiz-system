import socket
import struct
import json
import random
import time
import threading
import argparse

class MessageType:
    MSG_GENRE = 0
    MSG_QUESTION = 1
    MSG_ANSWER = 2
    MSG_FEEDBACK = 3
    MSG_LEADERBOARD_REQ = 4
    MSG_LEADERBOARD_RESP = 5
    MSG_QUIZ_OVER = 6
    MSG_ERROR = 7
    MSG_KEEP_ALIVE = 8
    MSG_ALIVE_OK = 9


GENRES = ["science", "history", "movies", "sports", "technology", "geography"]
ANSWERS = ['A', 'B', 'C', 'D']


def send_message(sock, msg_type, payload, client_id):
    """Encodes and sends a message according to the protocol."""
    if isinstance(payload, dict):
        payload_bytes = json.dumps(payload).encode('utf-8')
    elif isinstance(payload, str):
        payload_bytes = payload.encode('utf-8')
    else:
        payload_bytes = b''

    header = struct.pack('!BI', msg_type, len(payload_bytes))
    
    try:
        sock.sendall(header + payload_bytes)
    except (BrokenPipeError, ConnectionResetError):
        print(f"[Client {client_id}] [ERROR] Connection to server lost during send.")

def receiver_thread(sock, client_id, quiz_active_flag):
    """A dedicated thread to listen for and react to server messages for one client."""
    while quiz_active_flag.is_set():
        try:
            header_data = sock.recv(5)
            if not header_data:
                print(f"[Client {client_id}] [INFO] Server closed the connection.")
                quiz_active_flag.clear()
                break
            
            msg_type, payload_size = struct.unpack('!BI', header_data)
            payload_data = b''
            if payload_size > 0:
                received_len = 0
                while received_len < payload_size:
                    chunk = sock.recv(payload_size - received_len)
                    if not chunk:
                        raise ConnectionError("Connection lost while receiving payload.")
                    payload_data += chunk
                    received_len += len(chunk)

            if msg_type == MessageType.MSG_QUESTION:
                question = json.loads(payload_data)
                print(f"[Client {client_id}] <-- Received Question: {question['q'][:40]}...")
                time.sleep(random.uniform(1.5, 5.0))
                chosen_answer = random.choice(ANSWERS)
                answer_payload = {"answer": chosen_answer}
                print(f"[Client {client_id}] --> Answering '{chosen_answer}'")
                send_message(sock, MessageType.MSG_ANSWER, answer_payload, client_id)

            elif msg_type == MessageType.MSG_FEEDBACK:
                feedback = json.loads(payload_data)
                print(f"[Client {client_id}] <-- Feedback: Result={feedback['result']}, Score={feedback['score']}")

            elif msg_type == MessageType.MSG_QUIZ_OVER:
                print(f"[Client {client_id}] Quiz Over! Final Score: {payload_data.decode('utf-8')}")
                quiz_active_flag.clear()

            elif msg_type == MessageType.MSG_KEEP_ALIVE:
                print(f"[Client {client_id}] <-- KEEP_ALIVE, responding...")
                send_message(sock, MessageType.MSG_ALIVE_OK, "", client_id)

            elif msg_type == MessageType.MSG_ERROR:
                print(f"[Client {client_id}] [SERVER ERROR] {payload_data.decode('utf-8')}")
                quiz_active_flag.clear()
            
        except (struct.error, ConnectionError, ConnectionAbortedError, ConnectionResetError) as e:
            if quiz_active_flag.is_set():
                print(f"[Client {client_id}] [ERROR] Connection lost: {e}")
            quiz_active_flag.clear()
            break

def run_one_client(ip, port, client_id):
    """Contains the entire lifecycle for a single automated client."""
    quiz_active_flag = threading.Event()
    quiz_active_flag.set()

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        try:
            sock.connect((ip, port))
            print(f"[Client {client_id}] Connected to server at {ip}:{port}")
        except ConnectionRefusedError:
            print(f"[Client {client_id}] [FATAL] Connection refused. Is the server running?")
            return

        chosen_genre = random.choice(GENRES)
        print(f"[Client {client_id}] --> Selecting random genre: '{chosen_genre}'")
        send_message(sock, MessageType.MSG_GENRE, chosen_genre, client_id)

        listener = threading.Thread(target=receiver_thread, args=(sock, client_id, quiz_active_flag))
        listener.start()
        listener.join()
        print(f"[Client {client_id}] Thread finished.")


def main():
    """Main function to prompt for client count and launch threads."""
    parser = argparse.ArgumentParser(description="Interactive Multi-Client Quiz Tester")
    parser.add_argument("ip", type=str, help="Server IP address")
    parser.add_argument("port", type=int, help="Server port number")
    args = parser.parse_args()

    
    while True:
        try:
            num_clients_str = input("Enter the number of clients to run: ")
            num_clients = int(num_clients_str)
            if num_clients > 0:
                break
            else:
                print("Please enter a positive number.")
        except ValueError:
            print("Invalid input. Please enter a whole number.")
    

    print(f"\n--- Starting test with {num_clients} client(s) ---")
    
    threads = []
    for i in range(num_clients):
        thread = threading.Thread(target=run_one_client, args=(args.ip, args.port, i))
        threads.append(thread)
        thread.start()
        time.sleep(0.1)

    for thread in threads:
        thread.join()

    print("\n--- All clients have finished. Test complete. ---")

if __name__ == "__main__":
    main()