# Test class for the more complex builtin "history"
#     1. making sure it prints out the history as expected

from testutils import *

setup_tests()

#sending history to an empty list to make sure it prints correctly
expect_prompt()
sendline('history')
expect('   0 history', 'first history didnt pass')