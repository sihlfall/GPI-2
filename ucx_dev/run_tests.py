#!/usr/bin/env python3

import paramiko
import queue
import subprocess
import threading

LOCAL_RUN_OK = 0
LOCAL_RUN_ERR = -999

REMOTE_RUN_OK = 0
REMOTE_RUN_ERR = -999
VERBOSE = True

def print_verbose(msg):
  if VERBOSE:
    print(msg)

def execute_local_binary(binary_path, keystroke):
  try:
    # Start the binary using subprocess.Popen to have access to stdin, stdout, and stderr
    print(f"Executing local binary: {binary_path}...")
    process = subprocess.Popen(
      binary_path,                  # The command or binary to run
      stdin=subprocess.PIPE,         # Allow sending input to the process via stdin
      stdout=subprocess.PIPE,        # Capture the output
      stderr=subprocess.PIPE,        # Capture the error
      text=True                      # Work with text (strings) rather than bytes
    )

    # Send the keystroke to the binary via stdin
    print(f"Sending keystroke: {keystroke}")
    process.stdin.write(keystroke + '\n')
    process.stdin.flush()

    # Wait for the process to finish
    print("Waiting for the command to finish...")
    stdout_text, stderr_text = process.communicate()  # Read the output and error (if any)

    # Capture the return code (exit status)
    exit_status = process.returncode

    return (LOCAL_RUN_OK, (exit_status, stdout_text, stderr_text))

  except Exception as e:
    print(f"An error occurred: {e}")
    return (LOCAL_RUN_ERR, ())

def execute_remote_binary(result_queue, host, port, username, binary_path, keystroke):
  # ChatGPT was helpful in creating the following
  try:
    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    print_verbose(f"Connecting to {host}...")
    ssh.connect(host, port=port, username=username)
    
    print_verbose(f"Executing binary: {binary_path}...")
    stdin, stdout, stderr = ssh.exec_command(binary_path)
    
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

def start_in_thread(host, args):
  result_queue = queue.Queue()
  thread = threading.Thread(target=execute_remote_binary, args=(result_queue,) + args)
  thread.start()
  return (thread, result_queue)

host = '192.168.0.205'
port = 22  # Default SSH port
username = 'jbecker'

def test01_run():
  binary_path = '/home/cluster/tryout/test01'
  keystroke = 'q'  # The keystroke you want to send to the binary
  print('Running test01:')

  thread, result_queue = start_in_thread(
    execute_remote_binary,
    (host, port, username, binary_path, keystroke)
  )

  thread.join()

  remote_run_status, remote_run_result = result_queue.get()

  if remote_run_status == REMOTE_RUN_OK:
    exit_status, stdout_text, stderr_text = remote_run_result
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
  keystroke = 'q'  # The keystroke you want to send to the binary
  print('Running test01b:')

  local_run_status, local_run_result = execute_local_binary(binary_path, keystroke)

  if local_run_status == LOCAL_RUN_OK:
    exit_status, stdout_text, stderr_text = local_run_result
    if exit_status == 0:
      print('Success')
    else:
      print('Test failed')
      print(f'Exit code: {exit_status}')
      print(f'Remote stdout:\n{stdout_text}')
      print(f'Remote stderr:\n{stderr_text}')
  else:
    print("Remote run failed")

test01b_run()
