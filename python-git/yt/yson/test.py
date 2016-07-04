#!/usr/bin/python
#!-*-coding:utf-8-*-

import yt

import parser
import writer

import convert
import yson_to_bash

from StringIO import StringIO
import unittest

class PrintBashTest(unittest.TestCase):
    def yson_to_bash_test(self, input, correct_output, path=""):
        class Options(object):
            def __init__(self):
                self.sentinel = ""
                self.list_begin = ""
                self.list_separator = "\n"
                self.list_end = ""
                self.none_literal = "<None>"
                self.map_begin = ""
                self.map_separator = "\n"
                self.map_key_value_separator = "\t"
                self.map_end = ""
        yson_to_bash.options = Options()
        yson_to_bash.stdout = StringIO()
        yson_to_bash.print_bash(yson_to_bash.go_by_path(parser.loads(input), path), 3)
        self.assertEqual(yson_to_bash.stdout.getvalue(), correct_output)

    def test_print_bash(self):
        return # TODO(roizner): Tests are broken -- yson_to_bash.options are incorrect
        self.yson_to_bash_test("123", "123")
        self.yson_to_bash_test("[a; b; c]", "a\nb\nc")
        self.yson_to_bash_test("[{a=1; b=2}; {c=3; d=4}]", "a\t1\nb\t2\nc\t3\nd\t4")
        self.yson_to_bash_test("[{a=1; b=2}; {c=3; d=4}]", "c\t3\nd\t4", "1")
        self.yson_to_bash_test("[{a=1; b=2}; {c=3; d=4}]", "3", "1/c")

class YsonParserTestBase(object):
    def assert_equal(self, parsed, expected, attributes):
        if expected is None:
            assert isinstance(parsed, yt.yson.yson_types.YsonEntity)
            self.assertEqual(parsed.attributes, attributes)
        else:
            self.assertEqual(parsed, convert.to_yson_type(expected, attributes))

    def assert_parse(self, string, expected, attributes = {}):
        self.assert_equal(YsonParserTestBase.parser.loads(string), expected, attributes)
        stream = StringIO(string)
        self.assert_equal(YsonParserTestBase.parser.load(stream), expected, attributes)

    def test_quoted_string(self):
        self.assert_parse('"abc\\"\\n"', 'abc"\n')

    def test_unquoted_string(self):
        self.assert_parse('abc10', 'abc10')

    def test_binary_string(self):
        self.assert_parse('\x01\x06abc', 'abc')

    def test_int(self):
        self.assert_parse('64', 64)

    def test_uint(self):
        self.assert_parse('64u', 64)

    def test_binary_int(self):
        self.assert_parse('\x02\x81\x40', -(2 ** 12) - 1)

    def test_double(self):
        self.assert_parse('1.5', 1.5)

    def test_exp_double(self):
        self.assert_parse('1.73e23', 1.73e23)

    def test_binary_double(self):
        self.assert_parse('\x03\x00\x00\x00\x00\x00\x00\xF8\x3F', 1.5)

    def test_boolean(self):
        self.assert_parse('%false', False)
        self.assert_parse('%true', True)
        self.assert_parse('\x04', False)
        self.assert_parse('\x05', True)

    def test_empty_list(self):
        self.assert_parse('[ ]', [])

    def test_one_element_list(self):
        self.assert_parse('[a]', ['a'])

    def test_list(self):
        self.assert_parse('[1; 2]', [1, 2])

    def test_empty_map(self):
        self.assert_parse('{ }', {})

    def test_one_element_map(self):
        self.assert_parse('{a=1}', {'a': 1})

    def test_map(self):
        self.assert_parse('<attr1 = e; attr2 = f> {a = b; c = d}', {'a': 'b', 'c': 'd'}, {'attr1': 'e', 'attr2': 'f'})

    def test_entity(self):
        self.assert_parse('#', None)

    def test_nested(self):
        self.assert_parse(
            '''
            {
                path = "/home/sandello";
                mode = 755;
                read = [
                        "*.sh";
                        "*.py"
                       ]
            }
            ''',
            {'path' : '/home/sandello', 'mode' : 755, 'read' : ['*.sh', '*.py']})

    def test_convert_json_to_yson(self):
        x = convert.json_to_yson({
            "$value": {
                "x": {
                    "$value": 10,
                    "$attributes": {}
                },
                "y": {
                    "$value": 11,
                    "$attributes": {}
                },
                "z": u"Брюссельская капуста"
            },
            "$attributes": {
                "$value": "abc",
                "$attributes": {}
            }
        })

        z = str(bytearray(u"Брюссельская капуста", "utf-8"))
        self.assertEqual(dict(x), {"x": 10, "y": 11, "z": z})
        self.assertEqual(x.attributes, "abc")

        self.assertEqual(convert.json_to_yson("abc"), "abc")


    def test_convert_yson_to_json(self):
        from yt.yson import to_yson_type

        x = convert.yson_to_json({
            "a": to_yson_type(10, attributes={"attr": 1}),
            "b": to_yson_type(5.0, attributes={"attr": 2}),
            "c": to_yson_type("string", attributes={"attr": 3}),
            "d": to_yson_type(
                {
                    "key": [1, 2]
                },
                attributes={
                    "attr": 4,
                    "$xxx": "yyy",
                    "other_attr": to_yson_type(10, attributes={}),
                    u"ключ": None
                }),
            "e": to_yson_type(None, attributes={"x": "y"}),
            "f": to_yson_type(u"abacaba", attributes={"attr": 4})
        })

        self.assertEqual(x["a"], {"$value": 10, "$attributes": {"attr": 1}})
        self.assertEqual(x["b"], {"$value": 5.0, "$attributes": {"attr": 2}})
        self.assertEqual(x["c"], {"$value": "string", "$attributes": {"attr": 3}})
        self.assertEqual(x["d"], {"$value": {"key": [1, 2]}, "$attributes": {"attr": 4, "$$xxx": "yyy", "other_attr": 10, u"ключ": None}})
        self.assertEqual(x["e"], {"$value": None, "$attributes": {"x": "y"}})
        self.assertEqual(x["f"], {"$value": "abacaba", "$attributes": {"attr": 4}})
        self.assertEqual(set(x.keys()), set(["a", "b", "c", "d", "e", "f"]))


class TestParser(unittest.TestCase, YsonParserTestBase):
    YsonParserTestBase.parser = parser

class YsonWriterTestBase(object):
    def test_slash(self):
        self.assertTrue(
            self.writer.dumps({"key": "1\\"}, yson_format="text") in \
            [
                '{"key"="1\\\\";}',
                '{"key"="1\\\\"}',
            ])

    def test_boolean(self):
        dumps = self.writer.dumps

        self.assertEqual(dumps(False, boolean_as_string=True), '"false"')
        self.assertEqual(dumps(True, boolean_as_string=True), '"true"')
        self.assertEqual(dumps(False, boolean_as_string=False), "%false")
        self.assertEqual(dumps(True, boolean_as_string=False), "%true")

    def test_long_integers(self):
        dumps = self.writer.dumps

        from yt.yson import YsonUint64, YsonInt64

        long = 2 ** 63
        self.assertEqual('%su' % str(long), dumps(long))

        long = 2 ** 63 - 1
        self.assertEqual('%s' % str(long), dumps(long))
        self.assertEqual('%su' % str(long), dumps(YsonUint64(long)))

        long = -2 ** 63
        self.assertEqual('%s' % str(long), dumps(long))

        self.assertRaises(Exception, lambda: dumps(2 ** 64))
        self.assertRaises(Exception, lambda: dumps(-2 ** 63 - 1))
        self.assertRaises(Exception, lambda: dumps(YsonUint64(-2 ** 63)))
        self.assertRaises(Exception, lambda: dumps(YsonInt64(2 ** 63 + 1)))

class TestWriter(unittest.TestCase, YsonWriterTestBase):
    YsonWriterTestBase.writer = writer

class TestTypes(unittest.TestCase):
    def test_entity(self):
        self.assertEqual(yt.yson.yson_types.YsonEntity(), yt.yson.yson_types.YsonEntity())

class CommonTestBase(object):
    def test_long_integers(self):
        loads = CommonTestBase.parser.loads
        dumps = CommonTestBase.writer.dumps

        num = 1
        self.assertEqual("1", dumps(num))
        loaded = loads("1")
        self.assertEqual(1, loaded)
        self.assertTrue(isinstance(loaded, long))

        num = 2 ** 50
        loaded = loads(dumps(num))
        self.assertEqual(2 ** 50, loaded)
        self.assertTrue(isinstance(loaded, long))

        yson_num = "1u"
        loaded = loads(yson_num)
        self.assertEqual(1, loaded)
        self.assertTrue(isinstance(loaded, yt.yson.yson_types.YsonUint64))
        self.assertEqual("1u", dumps(loaded))

    def test_equalities(self):
        loads = CommonTestBase.parser.loads
        dumps = CommonTestBase.writer.dumps

        num = 1
        num_long = 1L
        lst = [1, 2, 3]
        s = "abc"
        f = 1.0
        d = {"x": 2}
        self.assertEqual(loads(dumps(num)), num_long)

        self.assertFalse(None == loads(dumps(num)))
        self.assertFalse(None == loads(dumps(lst)))
        self.assertFalse(None == loads(dumps(s)))
        self.assertFalse(None == loads(dumps(f)))

        self.assertFalse(lst == loads(dumps(f)))
        self.assertFalse(num == loads(dumps(s)))
        self.assertFalse(loads(dumps(d)) == s)

    def test_map_fragment(self):
        dumps = CommonTestBase.writer.dumps
        self.assertEqual('"a"="b";"c"="d";', dumps({"a": "b", "c": "d"}, yson_format="text", yson_type="map_fragment"))

    def test_invalid_attributes(self):
        dumps = CommonTestBase.writer.dumps

        obj = yt.yson.yson_types.YsonEntity()

        obj.attributes = None
        self.assertEqual("#", dumps(obj))

        obj.attributes = []
        self.assertRaises(Exception, lambda: dumps(obj))

class TestCommon(unittest.TestCase, CommonTestBase):
    CommonTestBase.writer = writer
    CommonTestBase.parser = parser

class TestYsonTypes(unittest.TestCase):
    def test_equalities(self):
        from yt.yson import YsonBoolean

        a = YsonBoolean(False)
        b = YsonBoolean(False)
        self.assertTrue(a == b)
        self.assertFalse(a != b)

        a.attributes["attr"] = 10
        b.attributes["attr"] = 20
        self.assertFalse(a == b)
        self.assertTrue(a != b)

if __name__ == "__main__":
    unittest.main()
