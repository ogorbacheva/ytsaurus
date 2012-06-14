import os

from yt_env import YTEnv, SANDBOX_ROOTDIR
from functools import wraps

import yt_commands

def _working_dir(test_name):
    path_to_test = os.path.join(SANDBOX_ROOTDIR, test_name)
    return os.path.join(path_to_test, "run")

class YTEnvSetup(YTEnv):

    @classmethod
    def setup_class(cls):
        test_name = cls.__name__
        path_to_test = os.path.join(SANDBOX_ROOTDIR, test_name)

        os.system('rm -rf ' + path_to_test)
        os.makedirs(path_to_test)

        path_to_run = os.path.join(path_to_test, "run")

        cls.path_to_test = path_to_test
        cls.Env = cls()
        cls.Env.setUp(path_to_run)

    @classmethod
    def teardown_class(cls):
        cls.Env.tearDown()

    def setup_method(self, method):
        path_to_test_case = os.path.join(self.path_to_test, method.__name__)

        os.makedirs(path_to_test_case)
        os.chdir(path_to_test_case)
        if self.Env.NUM_MASTERS > 0:
            yt_commands.set('//tmp', '{}')

# decorator form
ATTRS = [
    'NUM_MASTERS',
    'NUM_HOLDERS',
    'NUM_SCHEDULERS',
    'DELTA_MASTER_CONFIG',
    'DELTA_HOLDER_CONFIG',
    'DELTA_SCHEDULER_CONFIG',
    ]
def ytenv(**attrs):
    def make_decorator(f):
        @wraps(f)
        def wrapped(*args, **kw):
            env = YTEnv()
            for i in ATTRS:
                if i in attrs:
                    setattr(env, i, attrs.get(i))
            working_dir = _working_dir(f.__name__)
            env.setUp(working_dir)
            f(*args, **kw)
            env.tearDown()
        return wrapped
    return make_decorator