from .helpers import TEST_DIR, get_environment_for_binary_test

import yt.wrapper as yt

try:
    import yatest.common as yatest_common
except ImportError:
    yatest_common = None

import pytest
import tempfile

@pytest.mark.usefixtures("yt_env")
class TestIPython(object):
    def test_run_operation(self, yt_env):
        if yatest_common is None:
            pytest.skip()

        input_table = TEST_DIR + "/input_table"
        output_table = TEST_DIR + "/output_table"
        yt.write_table(input_table, [{"x": 1}])

        ipython = yatest_common.binary_path("yt/python/yt/wrapper/tests/yt_ipython/yt_ipython")
        _, script_filename = tempfile.mkstemp(dir=yt_env.env.path, suffix=".py")
        with open(script_filename, "w") as fout:
            fout.write(
                "def foo(row):\n"
                "    yield row\n"
                "import yt.wrapper as yt\n"
                "yt.run_map(foo, \"{input}\", \"{output}\")\n".format(
                    input=input_table,
                    output=output_table)
            )

        yatest_common.execute(
            [ipython, script_filename],
            env=get_environment_for_binary_test(yt_env),
            stdout="ipython.out",
            stderr="ipython.err")

        assert list(yt.read_table(output_table)) == [{"x": 1}]


