import pytest
import unittest

from yt_env_setup import YTEnvSetup
from yt_commands import *

##################################################################

class TestTableCommands(unittest.TestCase, YTEnvSetup):
    NUM_MASTERS = 3
    NUM_HOLDERS = 5

    # test that chunks are not available from chunk_lists
    # Issue #198
    def test_chunk_ids(self):
        create('table', '//tmp/t')
        write('//tmp/t', '{a=10}')

        chunk_ids = yson2py(ls('//sys/chunks'))
        assert len(chunk_ids) == 1
        chunk_id = chunk_ids[0]

        with pytest.raises(YTError): get('//sys/chunk_lists/"' + chunk_id + '"')
        remove('//tmp/t')


    def test_simple(self):
        create('table', '//tmp/table')

        assert read_table('//tmp/table') == []
        assert get('//tmp/table/@row_count') == '0'

        write('//tmp/table', '{b="hello"}')
        assert read_table('//tmp/table') == [{"b":"hello"}]
        assert get('//tmp/table/@row_count') == '1'

        write('//tmp/table', '{b="2";a="1"};{x="10";y="20";a="30"}')
        assert read_table('//tmp/table') == [{"b": "hello"}, {"a":"1", "b":"2"}, {"a":"30", "x":"10", "y":"20"}]
        assert get('//tmp/table/@row_count') == '3'

        remove('//tmp/table')

    def test_invalid_cases(self):
        create('table', '//tmp/table')

        # we can write only list or maps
        with pytest.raises(YTError): write('//tmp/table', 'string')
        with pytest.raises(YTError): write('//tmp/table', '100')
        with pytest.raises(YTError): write('//tmp/table', '3.14')
        with pytest.raises(YTError): write('//tmp/table', '<>')

        remove('//tmp/table')

    def test_row_index_selector(self):
        create('table', '//tmp/table')

        write('//tmp/table', '{a = 0}; {b = 1}; {c = 2}; {d = 3}')

        # closed ranges
        assert read_table('//tmp/table[#0:#2]') == [{'a': 0}, {'b' : 1}] # simple
        assert read_table('//tmp/table[#-1:#1]') == [{'a': 0}] # left < min
        assert read_table('//tmp/table[#2:#5]') == [{'c': 2}, {'d': 3}] # right > max

        assert read_table('//tmp/table[#1:#1]') == [] # left = right
        assert read_table('//tmp/table[#3:#1]') == [] # left > right

        # open ranges
        assert read_table('//tmp/table[:]') == [{'a': 0}, {'b' : 1}, {'c' : 2}, {'d' : 3}]
        assert read_table('//tmp/table[:#3]') == [{'a': 0}, {'b' : 1}, {'c' : 2}]
        assert read_table('//tmp/table[#2:]') == [{'c' : 2}, {'d' : 3}]

        remove('//tmp/table')

    def test_sorted_write(self):
        create('table', '//tmp/table')

        write('//tmp/table', '{key = 0}; {key = 1}; {key = 2}; {key = 3}', sorted_by='key')

        self.assertEqual(yson2py(get('//tmp/table/@sorted')), 'true')
        self.assertEqual(yson2py(get('//tmp/table/@key_columns')), ['key'])
        self.assertEqual(yson2py(get('//tmp/table/@row_count')), 4)

        remove('//tmp/table')

    def test_row_key_selector(self):
        create('table', '//tmp/table')

        v1 = {'s' : 'a', 'i': 0,    'd' : 15.5}
        v2 = {'s' : 'a', 'i': 10,   'd' : 15.2}
        v3 = {'s' : 'b', 'i': 5,    'd' : 20.}
        v4 = {'s' : 'b', 'i': 20,   'd' : 20.}
        v5 = {'s' : 'c', 'i': -100, 'd' : 10.}

        values = yson.dumps([v1, v2, v3, v4, v5])
        values = values[1:-1] # remove surrounding [ ]

        write('//tmp/table', values, sorted_by='s;i;d')

        # possible empty ranges
        assert read_table('//tmp/table[a : a]') == []
        assert read_table('//tmp/table[b : a]') == []
        assert read_table('//tmp/table[(c, 0) : (a, 10)]') == []
        assert read_table('//tmp/table[(a, 10, 1e7) : (b, )]') == []

        # some typical cases
        assert read_table('//tmp/table[(a, 4) : (b, 20, 18.)]') == [v2, v3]
        assert read_table('//tmp/table[(a, 4) : (b, 20, 18.)]') == [v2, v3]
       

        remove('//tmp/table')


    def test_column_selector(self):
        create('table', '//tmp/table')

        write('//tmp/table', '{a = 1; aa = 2; b = 3; bb = 4; c = 5}')

        # empty columns
        assert read_table('//tmp/table{}') == [{}]

        # single columms
        assert read_table('//tmp/table{a}') == [{'a' : 1}]
        assert read_table('//tmp/table{a, }') == [{'a' : 1}] # extra comma
        assert read_table('//tmp/table{a, a}') == [{'a' : 1}]
        assert read_table('//tmp/table{c, b}') == [{'b' : 3, 'c' : 5}]

        # range columns
        # closed ranges
        with pytest.raises(YTError): read_table('//tmp/table{a:a}')  # left = right
        with pytest.raises(YTError): read_table('//tmp/table{b:a}')  # left > right

        assert read_table('//tmp/table{aa:b}') == [{'aa' : 2}]  # (+, +)
        assert read_table('//tmp/table{aa:bx}') == [{'aa' : 2, 'b' : 3, 'bb' : 4}]  # (+, -)
        assert read_table('//tmp/table{aaa:b}') == [{}]  # (-, +)
        assert read_table('//tmp/table{aaa:bx}') == [{'b' : 3, 'bb' : 4}] # (-, -)

        # open ranges
        # from left
        assert read_table('//tmp/table{:aa}') == [{'a' : 1}] # + 
        assert read_table('//tmp/table{:aaa}') == [{'a' : 1, 'aa' : 2}] # -

        # from right
        assert read_table('//tmp/table{bb:}') == [{'bb' : 4, 'c' : 5}] # + 
        assert read_table('//tmp/table{bz:}') == [{'c' : 5}] # -
        assert read_table('//tmp/table{xxx:}') == [{}]

        # fully open
        assert read_table('//tmp/table{:}') == [{'a' :1, 'aa': 2,  'b': 3, 'bb' : 4, 'c': 5}]

        remove('//tmp/table')

        # mixed column keys
        # TODO(panin): check intersected columns

    def test_shared_locks_two_chunks(self):
        create('table', '//tmp/table')
        tx_id = start_transaction()

        write('//tmp/table', '{a=1}', tx=tx_id)
        write('//tmp/table', '{b=2}', tx=tx_id)

        assert read_table('//tmp/table') == []
        assert read_table('//tmp/table', tx=tx_id) == [{'a':1}, {'b':2}]

        commit_transaction(tx=tx_id)
        assert read_table('//tmp/table') == [{'a':1}, {'b':2}]

        remove('//tmp/table')

    def test_shared_locks_three_chunks(self):
        create('table', '//tmp/table')
        tx_id = start_transaction()

        write('//tmp/table', '{a=1}', tx=tx_id)
        write('//tmp/table', '{b=2}', tx=tx_id)
        write('//tmp/table', '{c=3}', tx=tx_id)
        
        assert read_table('//tmp/table') == []
        assert read_table('//tmp/table', tx=tx_id) == [{'a':1}, {'b':2}, {'c' : 3}]

        commit_transaction(tx=tx_id)
        assert read_table('//tmp/table') == [{'a':1}, {'b':2}, {'c' : 3}]

        remove('//tmp/table')



    def test_shared_locks_parallel_tx(self):
        create('table', '//tmp/table')

        write('//tmp/table', '{a=1}')

        tx1 = start_transaction()
        tx2 = start_transaction()

        write('//tmp/table', '{b=2}', tx=tx1)

        write('//tmp/table', '{c=3}', tx=tx2)
        write('//tmp/table', '{d=4}', tx=tx2)

        # check which records are seen from different transactions
        assert read_table('//tmp/table') == [{'a' : 1}]
        assert read_table('//tmp/table', tx = tx1) == [{'a' : 1}, {'b': 2}]
        assert read_table('//tmp/table', tx = tx2) == [{'a' : 1}, {'c': 3}, {'d' : 4}]

        commit_transaction(tx = tx2)
        assert read_table('//tmp/table') == [{'a' : 1}, {'c': 3}, {'d' : 4}]
        assert read_table('//tmp/table', tx = tx1) == [{'a' : 1}, {'b': 2}]
        
        # now all records are in table in specific order
        commit_transaction(tx = tx1)
        assert read_table('//tmp/table') == [{'a' : 1}, {'c': 3}, {'d' : 4}, {'b' : 2}]

        
