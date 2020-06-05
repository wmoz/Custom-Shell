#!/usr/bin/python
'''
Job control tests for shell. Similar to previous tests
on single commands but with pipelines. Common errors
would be bad process group management, and not giving
proper foreground group to terminal.
'''

from testutil import *
from tempfile import mkstemp


setup_tests()
   
expect_prompt()

sendline('sleep 10 | sleep 9')
time.sleep(0.5)
sendcontrol('z')
expect_prompt()

run_builtin('jobs')
job = parse_job_line()

assert 'sleep' in job.command
assert job.status == 'stopped'

expect_prompt()
run_builtin('bg', str(job.id))
expect_prompt()

run_builtin('jobs')
job = parse_job_line()

assert 'sleep' in job.command
assert job.status == 'running'

expect_prompt()
run_builtin('kill', str(job.id))
expect_prompt()


sendline('ps --ppid {0} -o pid,cmd,stat --no-headers | wc -l'.format(get_shell_pid()))
expect('2')
expect_prompt()


sendline('sleep 100 | sleep 99 | sleep 98 | sleep 97 &')
expect_prompt()
sendline('ps --ppid {0} -o pid,stat,cmd --no-headers'.format(get_shell_pid()))

count = 0
while count < 4:
    pid, stat, cmd = expect_regex('(\d+)\s+(\w)\s+(\w+)')
    if 'sleep' in cmd:
        assert stat != 'T'
        count += 1

expect_prompt()
run_builtin('jobs')
job = parse_job_line()

assert 'sleep' in job.command
assert job.status == 'running'

expect_prompt()
run_builtin('stop', str(job.id))
expect_prompt()

sendline('ps --ppid {0} -o pid,stat,cmd --no-headers'.format(get_shell_pid()))
count = 0
while count < 4:
    pid, stat, cmd = expect_regex('(\d+)\s+(\w)\s+(\w+)')
    if 'sleep' in cmd:
        assert stat == 'T'
        count += 1

run_builtin('bg', str(job.id))
expect_prompt()

run_builtin('kill', str(job.id))
expect_prompt()

test_success()

