#Tests the custome prompt to make sure it meets the requirement 

from getpass import getuser
import os
import pwd
import socket
from testutils import *

setup_tests()

#get the hostname
hostname = socket.gethostname()
user = pwd.getpwuid(os.getuid())[0]

#get the path
path = os.getcwd()
#get the current directory only
cwd = os.path.basename(path)

#expected string
expected = '<' + user + '@' + hostname + ' ' + cwd + '>$ '

expect_exact(expected, 'error didnt pass')
test_success()