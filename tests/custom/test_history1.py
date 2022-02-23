# Test class for the more complex builtin "history"
#     1. making sure it prints out the history as expected
#     2. making sure that commands such as !! and !l work correctly

from testutils import *

setup_tests()

#sending ls 
expect_prompt()
sendline('ls')

#sending sleep
expect_prompt()
sendline('sleep 1')

#checking if history will print the expected history
#after adding commands into the list
expect_prompt()
sendline('history')
expect('   0 ls', 'first history didnt pass')
expect('   1 sleep 1', 'first history didnt pass')
expect('   2 history', 'first history didnt pass')

#checking if !! will return the previous command which is history
expect_prompt()
sendline('!!')
expect('   0 ls', 'second history didnt pass')
expect('   1 sleep 1', 'second history didnt pass')
expect('   2 history', 'second history didnt pass')
expect('   3 history', 'second history didnt pass')

#sending !s which is sleep
expect_prompt()
sendline('!s')

#testing if !h will print the history
expect_prompt()
sendline('!h')
expect('   0 ls', 'third history didnt pass')
expect('   1 sleep 1', 'third history didnt pass')
expect('   2 history', 'third history didnt pass')
expect('   3 history', 'third history didnt pass')
expect('   4 sleep 1', 'third history didnt pass')
expect('   5 history', 'third history didnt pass')

#testing the up arrow cursor
expect_prompt()
sendline('\x1B[A')
expect('history', '4th history didnt pass')

#sending sleep to test the up arrow with a different command
expect_prompt()
sendline('sleep 1')

#testing the up arrow cursor with sleep 
expect_prompt()
sendline('\x1B[A')
expect('sleep 1', '5th history didnt pass')

#testing the down arrow cursor
expect_prompt()
sendline('\x1B[B')
expect('', '6th history didnt pass')