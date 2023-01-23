from collections import defaultdict
import os
import time
import signal
import tempfile
import subprocess
import shutil
import socket
import struct

SERVER_PORT = 5555
SERVER_BINARY = './server'
CLIENT_BINARY = './client'
TEST_FOLDER = "./random_files"


def start_server():
    server = subprocess.Popen([SERVER_BINARY, f"{SERVER_PORT}"], stdout=subprocess.PIPE)
    return server

def _generate_folder_with_random_files(number_of_files, file_size=1024):
    shutil.rmtree(TEST_FOLDER, ignore_errors=True)
    os.mkdir(TEST_FOLDER)

    for _ in range(number_of_files):
        tmp = tempfile.NamedTemporaryFile(dir=TEST_FOLDER, delete=False)
        tmp.write(os.urandom(file_size))

def _run_client_on_file(file_name, number_of_readable_chars):
    p = subprocess.run([CLIENT_BINARY, "127.0.0.1", f"{SERVER_PORT}", file_name],
                       capture_output=True, text=True)

    if p.stderr:
        raise Exception(f"FAILED: {p.stderr}")

    assert p.stdout == f"# of printable characters: {number_of_readable_chars}\n", f"file name: {file_name}\nexpected number of readable: {number_of_readable_chars}\nclient output: {p.stdout}\n"

def _run_file_on_client_async(file_name):
    p = subprocess.Popen([CLIENT_BINARY, "127.0.0.1", f"{SERVER_PORT}", file_name])
    return p

def run_client_on_folder(should_run_async=False):
    printalbe_characters = defaultdict(int)

    for file in os.listdir(TEST_FOLDER):
        file_full_path = f"{TEST_FOLDER}/{file}"
        with open(file_full_path, 'rb') as f:
            current_printalbe_characters = 0

            while (byte := f.read(1)):
                if b'\x20' <= byte <= b'\x7E':
                    printalbe_characters[byte.decode("ascii")] += 1
                    current_printalbe_characters += 1
            if should_run_async:
                _run_file_on_client_async(file_full_path)
            else:
                _run_client_on_file(file_full_path, current_printalbe_characters)

    return printalbe_characters

def parse_server_output(server, printalbe_characters):
    server.send_signal(signal.SIGINT)
    server_stdout, _ = server.communicate()
    # print(server_stdout.decode("ascii"))

    for line in server_stdout.decode('ascii').split('\n')[:-1]:
        symbol = line[6:7]
        count = int(line[11:].split(' ')[0])
        assert printalbe_characters[symbol] == count, f"Wrong count for symbol, symbol: '{symbol}' expected count: {printalbe_characters[symbol]}, actual count: {count}, line: {line}"

def test_client_wrong_arguments():
    p = subprocess.run([CLIENT_BINARY, "127.0.0.1", f"{SERVER_PORT}", "hjasdfkghaklsdf"], capture_output=True, text=True)
    assert p.stderr != '', "failed chaking unknown file"

    os.system("echo 1 > ./file_for_tests")
    p = subprocess.run([CLIENT_BINARY, "127.0.0.1", f"{SERVER_PORT}", "./file_for_tests"], capture_output=True, text=True)
    assert p.stderr != '', "Should have failed to connect"

    p = subprocess.run([CLIENT_BINARY, ], capture_output=True, text=True)
    assert p.stderr != '', "Should have failed upon wrong number of arguments"

def test_server_wrong_arguments():
    p = subprocess.run([SERVER_BINARY], capture_output=True, text=True)
    assert p.stderr != '', "Should have failed upon wrong number of arguments"


def test_wrong_arguments():
    print("Starting Testing wrong aruments")
    test_client_wrong_arguments()
    test_server_wrong_arguments()
    print("Testing wrong aruments - PASSED")

def test_sigint_in_the_middle_of_accept():
    try:
        server = start_server()
        time.sleep(5)
        parse_server_output(server, defaultdict(int))
    finally:
        server.kill()

def test_sigint_in_the_middle_big_file_upload():
    try:
        server = start_server()
        time.sleep(5)
        _generate_folder_with_random_files(1, 1024 * 1000)
        printalbe_characters = run_client_on_folder(should_run_async=True)
        time.sleep(1)

        parse_server_output(server, printalbe_characters)

    finally:
        server.kill()

def test_singal_handler():
    print("Starting Testing signal handler")
    test_sigint_in_the_middle_of_accept()
    test_sigint_in_the_middle_big_file_upload()
    print("Testing signal handler - PASSED")

def python_client_run(should_send_all_message = False):
    try:
        server = start_server()
        _generate_folder_with_random_files(3)
        printalbe_characters = run_client_on_folder()

        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.connect(("127.0.0.1", SERVER_PORT))
            message = b"Hello, world"
            big_en_message_size = (len(message)).to_bytes(8, "big")

            s.send(big_en_message_size)

            if should_send_all_message:
                s.send(message)
                for b in message.decode("ascii"):
                    printalbe_characters[str(b)] += 1

            else:
                s.send(message[:-2])

        parse_server_output(server, printalbe_characters)

    finally:
        server.kill()

def test_client_fail():
    print("Starting testing client disconnect")
    python_client_run(True)
    python_client_run(False)
    print("Testing client fails - PASSED")


def happy_flow_tests():
    print("Starting happy flow test")
    try:
        server = start_server()
        _generate_folder_with_random_files(30)

        printalbe_characters = run_client_on_folder()
        parse_server_output(server, printalbe_characters)
        print("happy flow test - PASSED")
    except Exception as e:
        print("happy flow test - FAILED")
        raise e
    finally:
        server.kill()

def compile():
    gcc_command = ["gcc", "-O3", "-D_DEFAULT_SOURCE", "-Wall", "-std=c11"]
    compile_server = gcc_command + ['pcc_server.c', "-o", "server"]
    compile_client = gcc_command + ['pcc_client.c', "-o", "client"]

    subprocess.run(compile_server)
    subprocess.run(compile_client)


def run_tests():
    compile()
    test_wrong_arguments()
    happy_flow_tests()
    test_singal_handler()
    test_client_fail()

if __name__ == '__main__':
    run_tests()
