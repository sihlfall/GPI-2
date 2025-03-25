#!/usr/bin/env python3

import getpass
import os
import paramiko
import shlex
import subprocess
import time

VERBOSE = True
REPORT_OUTPUT = True
DEFAULT_SSH_PORT = 22

LOCAL_BINARY_DIR='./bin'
REMOTE_BINARY_DIR='/home/cluster/tryout'

host = '192.168.0.205'
port = DEFAULT_SSH_PORT
username = getpass.getuser()

def print_verbose(msg):
  if VERBOSE:
    print(msg)

def conclude_test_success():
  print('\033[32mSuccess\033[0m')

def conclude_test_failure():
  print('\033[31mFailed\033[0m')

def conclude_test(success):
  if success:
    conclude_test_success()
  else:
    conclude_test_failure()

def execute_local_binary(binary_path):
  try:
    print_verbose(f"Executing local binary: {binary_path}...")
    process = subprocess.Popen(
      binary_path,                  # The command or binary to run
      stdin=subprocess.PIPE,         # Allow sending input to the process via stdin
      stdout=subprocess.PIPE,        # Capture the output
      stderr=subprocess.PIPE,        # Capture the error
      text=True                      # Work with text (strings) rather than bytes
    )

    def communicate(input=None):
      stdout_text, stderr_text = process.communicate(input=input)
      exit_status = process.returncode
      return (exit_status, stdout_text, stderr_text)
    
    return (process, communicate)

  except Exception as e:
    print_verbose(f"An error occurred: {e}")
    return (None, None)

def execute_remote_binary(binary, host='127.0.0.1', port=DEFAULT_SSH_PORT, username=None):
  # ChatGPT was helpful in creating the following
  try:
    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    print_verbose(f"Connecting to {host}...")
    ssh.connect(host, port=port, username=username)
    
    cmd = shlex.join(binary)
    print_verbose(f"Executing remote command: {cmd}...")
    stdin, stdout, stderr = ssh.exec_command(cmd)

    class Process:
      def __init__(self, stdin, stdout, stderr):
        self.stdin = stdin
        self.stdout = stdout
        self.stderr = stderr

    process = Process(stdin, stdout, stderr)

    def communicate(input=None):
      if input is not None:
        process.stdin.write(input)
        process.stdin.flush()
    
      print_verbose("Waiting for the command to finish...")
      exit_status = stdout.channel.recv_exit_status()
      stdout_text = stdout.read().decode('utf-8')
      stderr_text = stderr.read().decode('utf-8')

      ssh.close()
      print_verbose("SSH connection closed.")

      return (exit_status, stdout_text, stderr_text)            
    
    return (process, communicate)
      
  except Exception as e:
    print(f"An error occurred: {e}")
    return (None, None)

def test01_run():
  binary_name = 'test01'
  binary_path = os.path.join(REMOTE_BINARY_DIR, binary_name)
  print('Running test01:')
  success = False

  process, communicate = execute_remote_binary(binary_path, host=host, port=port, username=username)

  if process is not None:
    exit_status, stdout_text, stderr_text = communicate(input='q\n')
    if exit_status != 0 or REPORT_OUTPUT:
      print(f'Exit code: {exit_status}')
      print(f'Remote stdout:\n{stdout_text}')
      print(f'Remote stderr:\n{stderr_text}')
    success = exit_status == 0
  else:
    print("Remote run failed")
  conclude_test(success)

def test01b_run():
  binary_name = 'test01'
  binary_path = os.path.join(LOCAL_BINARY_DIR, binary_name)
  print('Running test01b:')
  success = False

  process, communicate = execute_local_binary(binary_path)

  if process is not None:
    exit_status, stdout_text, stderr_text = communicate(input='q\n')
    if exit_status != 0 or REPORT_OUTPUT:
      print(f'Exit code: {exit_status}')
      print(f'Remote stdout:\n{stdout_text}')
      print(f'Remote stderr:\n{stderr_text}')
    success = exit_status == 0
  else:
    print("Local run failed")
  conclude_test(success)

def test02_run():
  binary_name = 'test02'
  local_binary_path = os.path.join(LOCAL_BINARY_DIR, binary_name)
  print('Running test02 (local/local):')
  success = False

  server_process, server_communicate = execute_local_binary([ local_binary_path, 's', '9000' ])
  time.sleep(0.1) # TODO: Wait for some "OK" message from server
  client_process, client_communicate = execute_local_binary([ local_binary_path, 'c', '127.0.0.1', '9000' ])

  if server_process is not None and client_process is not None:
    client_exit_status, client_stdout_text, client_stderr_text = client_communicate()
    server_exit_status, server_stdout_text, server_stderr_text = server_communicate(input='q\n')

    if server_exit_status != 0 or REPORT_OUTPUT:
      print(f'Server exit code: {server_exit_status}')
      print(f'Server stdout:\n{server_stdout_text}')
      print(f'Server stderr:\n{server_stderr_text}')
    if client_exit_status != 0 or REPORT_OUTPUT:
      print(f'Client exit code: {client_exit_status}')
      print(f'Client stdout:\n{client_stdout_text}')
      print(f'Client stderr:\n{client_stderr_text}')
    success = server_exit_status == 0 and client_exit_status == 0
  else:
    if server_process is None:
      print("Server run failed")
    if client_process is None:
      print("Client run failed")
  conclude_test(success)

def test02b_run():
  binary_name = 'test02'
  local_binary_path = os.path.join(LOCAL_BINARY_DIR, binary_name)
  remote_binary_path = os.path.join(REMOTE_BINARY_DIR, binary_name)
  print('Running test02 (remote/local):')
  success = False

  server_process, server_communicate = execute_remote_binary(
    [ remote_binary_path, 's', '19000' ],
    host=host, username=username
  )
  time.sleep(0.1) # TODO: Wait for some "OK" message from server
  client_process, client_communicate = execute_local_binary([ local_binary_path, 'c', '192.168.0.205', '19000' ])

  if server_process is not None and client_process is not None:
    client_exit_status, client_stdout_text, client_stderr_text = client_communicate()
    server_exit_status, server_stdout_text, server_stderr_text = server_communicate(input='q\n')

    if server_exit_status != 0 or REPORT_OUTPUT:
      print(f'Server exit code: {server_exit_status}')
      print(f'Server stdout:\n{server_stdout_text}')
      print(f'Server stderr:\n{server_stderr_text}')
    if client_exit_status != 0 or REPORT_OUTPUT:
      print(f'Client exit code: {client_exit_status}')
      print(f'Client stdout:\n{client_stdout_text}')
      print(f'Client stderr:\n{client_stderr_text}')
    success = server_exit_status == 0 and client_exit_status == 0
  else:
    if server_process is None:
      print("Server run failed")
    if client_process is None:
      print("Client run failed")
  conclude_test(success)

test02_run()
