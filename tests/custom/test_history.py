# Test class for the more complex builtin "history"
#     1. making sure it prints out the history as expected
#     2. making sure that commands such as !! and !l work correctly

#import sys, imp, atexit, pexpect, signal, time, threading
from testutils import *

setup_tests()

expect_prompt()
sendline('history')
expect('   0 history', 'first history didnt pass')