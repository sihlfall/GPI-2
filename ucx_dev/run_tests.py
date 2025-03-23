#!/usr/bin/env python3

import paramiko
import queue
import shlex
import subprocess
import threading
import time

LOCAL_RUN_OK = 0
LOCAL_PIPES = 1
LOCAL_RUN_ERR = -999

REMOTE_RUN_OK = 0
REMOTE_PIPES = 1
REMOTE_RUN_ERR = -999
VERBOSE = True
DEFAULT_SSH_PORT = 22

def print_verbose(msg):
  if VERBOSE:
    print(msg)

def execute_local_binary2(binary_path):
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


def execute_remote_binary2(binary, host='127.0.0.1', port=DEFAULT_SSH_PORT, username=None):
  # ChatGPT was helpful in creating the following
  try:
    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    print_verbose(f"Connecting to {host}...")
    ssh.connect(host, port=port, username=username)
    
    cmd = shlex.join(binary)
    print_verbose(f"Executing command: {cmd}...")
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


def execute_local_binary(result_queue, binary):
  try:
    print_verbose(f"Executing local binary: {binary_path}...")
    process = subprocess.Popen(
      binary,                  # The command or binary to run
      stdin=subprocess.PIPE,         # Allow sending input to the process via stdin
      stdout=subprocess.PIPE,        # Capture the output
      stderr=subprocess.PIPE,        # Capture the error
      text=True                      # Work with text (strings) rather than bytes
    )

    result_queue.put( (LOCAL_PIPES, process) )

    while process.poll() is None:
      time.sleep(0.05)

    print_verbose("Finished")

  except Exception as e:
    print_verbose(f"An error occurred: {e}")
    return (LOCAL_RUN_ERR, ())

def local_communicate(process, input):
  stdout_text, stderr_text = process.communicate(input=input)
  exit_status = process.returncode
  return (exit_status, stdout_text, stderr_text)

def obsolete():
  if keystroke is not None:
    # Send the keystroke to the binary via stdin
    print_verbose(f"Sending keystroke: {keystroke}")
    process.stdin.write(keystroke + '\n')
    process.stdin.flush()
  print_verbose("Waiting for the command to finish...")

  result_queue.put( (LOCAL_RUN_OK, (exit_status, stdout_text, stderr_text)) )

def execute_remote_binary(result_queue, host, port, username, binary_path, keystroke):
  # ChatGPT was helpful in creating the following
  try:
    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    print_verbose(f"Connecting to {host}...")
    ssh.connect(host, port=port, username=username)
    
    print_verbose(f"Executing binary: {binary_path}...")
    stdin, stdout, stderr = ssh.exec_command(binary_path)
    result_queue.put( (REMOTE_PIPES, (ssh, stdin, stdout, stderr)) )

    if keystroke is not None:
      print_verbose(f"Sending keystroke: {keystroke}")
      stdin.write(keystroke + '\n')
      stdin.flush()
    
    print_verbose("Waiting for the command to finish...")
    exit_status = stdout.channel.recv_exit_status()
    stdout_text = stdout.read().decode('utf-8')
    stderr_text = stderr.read().decode('utf-8')
            
    ssh.close()
    print_verbose("SSH connection closed.")

    result_queue.put( (REMOTE_RUN_OK, (
      exit_status,
      stdout_text,
      stderr_text
    )))
      
  except Exception as e:
    print(f"An error occurred: {e}")
    result_queue.put( (REMOTE_RUN_ERR, ()) )

def remote_communicate(process, input=None):
  (stdin, stdout, stderr) = process
  if input is not None:
    stdin.write(input)
    stdin.flush()


def start_in_thread(fn, args):
  result_queue = queue.Queue()
  thread = threading.Thread(target=fn, args=(result_queue,) + args)
  thread.start()
  return (thread, result_queue)

def run(fn, args):
  result_queue = queue.Queue()
  fn (result_queue, *args)
  return result_queue

host = '192.168.0.205'
port = 22  # Default SSH port
username = 'jbecker'

def test01_run():
  binary_path = '/home/cluster/tryout/test01'
  keystroke = 'q'
  print('Running test01:')

  process, communicate = execute_remote_binary2(binary_path, host=host, port=port, username=username)

  if process is not None:
    exit_status, stdout_text, stderr_text = communicate(input='q\n')
    if exit_status == 0:
      print('Success')
    else:
      print('Test failed')
      print(f'Exit code: {exit_status}')
      print(f'Remote stdout:\n{stdout_text}')
      print(f'Remote stderr:\n{stderr_text}')
  else:
    print("Remote run failed")

def test01b_run():
  binary_path = './bin/test01'
  print('Running test01b:')

  process, communicate = execute_local_binary2(binary_path)

  if process is not None:
    exit_status, stdout_text, stderr_text = communicate(input='q\n')
    if exit_status == 0:
      print('Success')
    else:
      print('Test failed')
      print(f'Exit code: {exit_status}')
      print(f'Remote stdout:\n{stdout_text}')
      print(f'Remote stderr:\n{stderr_text}')
  else:
    print("Local run failed")

def test02_run():
  local_binary_path = './bin/test02'
  print('Running test02 (local/local):')

  server_process, server_communicate = execute_local_binary2([ local_binary_path, 's', '9000' ])
  client_process, client_communicate = execute_local_binary2([ local_binary_path, 'c', '127.0.0.1', '9000' ])

  if server_process is not None and client_process is not None:
    client_exit_status, client_stdout_text, client_stderr_text = client_communicate()
    server_exit_status, server_stdout_text, server_stderr_text = server_communicate(input='q\n')

    if server_exit_status == 0 and client_exit_status == 0:
      print('Success')
    else:
      print('Test failed')
      if server_exit_status != 0:
        print(f'Server exit code: {server_exit_status}')
        print(f'Server stdout:\n{server_stdout_text}')
        print(f'Server stderr:\n{server_stderr_text}')
      if client_exit_status != 0:
        print(f'Client exit code: {client_exit_status}')
        print(f'Client stdout:\n{client_stdout_text}')
        print(f'Client stderr:\n{client_stderr_text}')
  else:
    if server_process is None:
      print("Server run failed")
    if client_process is None:
      print("Client run failed")

def test02b_run():
  local_binary_path = './bin/test02'
  remote_binary_path = '/home/cluster/tryout/test02'
  print('Running test02 (remote/local):')

  server_process, server_communicate = execute_remote_binary2(
    [ remote_binary_path, 's', '9000' ],
    host=host, username=username
  )
  client_process, client_communicate = execute_local_binary2([ local_binary_path, 'c', '192.168.0.205', '9000' ])

  if server_process is not None and client_process is not None:
    client_exit_status, client_stdout_text, client_stderr_text = client_communicate()
    server_exit_status, server_stdout_text, server_stderr_text = server_communicate(input='q\n')

    if server_exit_status == 0 and client_exit_status == 0:
      print('Success')
    else:
      print('Test failed')
      if server_exit_status != 0:
        print(f'Server exit code: {server_exit_status}')
        print(f'Server stdout:\n{server_stdout_text}')
        print(f'Server stderr:\n{server_stderr_text}')
      if client_exit_status != 0:
        print(f'Client exit code: {client_exit_status}')
        print(f'Client stdout:\n{client_stdout_text}')
        print(f'Client stderr:\n{client_stderr_text}')
  else:
    if server_process is None:
      print("Server run failed")
    if client_process is None:
      print("Client run failed")


test02b_run()
