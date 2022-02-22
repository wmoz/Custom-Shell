# Test class for the more complex builtin "history"
#     1. making sure it prints out the history as expected
#     2. making sure that commands such as !! and !l work correctly

import sys, imp, atexit, pexpect, signal, time, threading
from testutils import *

setup_tests()

expect_prompt()
sendline('ls')

expect_prompt()
sendline('sleep 1')

expect_prompt()
sendline('history')
expect('   0 ls\n   1 sleep 1\n   2 history', 'first history didnt pass') #something goes in here to check output with the expected

# expect_prompt()
# sendline('!!')
# expect() #something goes in here to check output with the expected

# expect_prompt()
# sendline('!l')
# expect() #something goes in here to check output with the expected

