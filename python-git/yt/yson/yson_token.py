from .common import YsonError

from yt.common import flatten

from yt.packages.six.moves import map as imap

TOKEN_LITERAL = 0
TOKEN_SLASH = 1
TOKEN_AMPERSAND = 2
TOKEN_AT = 3
TOKEN_ASTERISK = 4
TOKEN_START_OF_STREAM = 5
TOKEN_END_OF_STREAM = 6
TOKEN_RANGE = 7
TOKEN_SEMICOLON = 8
TOKEN_EQUALS = 9
TOKEN_LEFT_BRACE = 10
TOKEN_RIGHT_BRACE = 11
TOKEN_HASH = 12
TOKEN_LEFT_BRACKET = 13
TOKEN_RIGHT_BRACKET = 14
TOKEN_LEFT_ANGLE = 15
TOKEN_RIGHT_ANGLE = 16
TOKEN_LEFT_PARENTHESIS = 17
TOKEN_RIGHT_PARENTHESIS = 18
TOKEN_COLON = 19
TOKEN_COMMA = 20
TOKEN_STRING = 21
TOKEN_INT64 = 22
TOKEN_UINT64 = 23
TOKEN_DOUBLE = 24
TOKEN_BOOLEAN = 25
TOKEN_SPECIAL = 26

def char_to_token_type(char):
    tokens = {
        ";": TOKEN_SEMICOLON,
        "=": TOKEN_EQUALS,
        "{": TOKEN_LEFT_BRACE,
        "}": TOKEN_RIGHT_BRACE,
        "#": TOKEN_HASH,
        "[": TOKEN_LEFT_BRACKET,
        "]": TOKEN_RIGHT_BRACKET,
        "<": TOKEN_LEFT_ANGLE,
        ">": TOKEN_RIGHT_ANGLE,
        "(": TOKEN_LEFT_PARENTHESIS,
        ")": TOKEN_RIGHT_PARENTHESIS,
        ":": TOKEN_COLON,
        ",": TOKEN_COMMA,
        "/": TOKEN_SLASH,
        "@": TOKEN_AT,
        "&": TOKEN_AMPERSAND,
        "*": TOKEN_ASTERISK
    }
    if char not in tokens:
        return TOKEN_END_OF_STREAM
    return tokens[char]

def token_type_to_string(token):
    names = {
        TOKEN_LITERAL: "Literal",
        TOKEN_SLASH: "Slash",
        TOKEN_AMPERSAND: "Ampersand",
        TOKEN_AT: "At",
        TOKEN_ASTERISK: "Asterisk",
        TOKEN_START_OF_STREAM: "Start-of-stream",
        TOKEN_END_OF_STREAM: "End-of-stream",
        TOKEN_RANGE: "Range",
        TOKEN_SEMICOLON: "Semicolon",
        TOKEN_EQUALS: "Equals",
        TOKEN_LEFT_BRACE: "Left-brace",
        TOKEN_RIGHT_BRACE: "Right-brace",
        TOKEN_HASH: "Hash",
        TOKEN_LEFT_BRACKET: "Left-bracket",
        TOKEN_RIGHT_BRACKET: "Right-bracket",
        TOKEN_LEFT_ANGLE: "Left-angle",
        TOKEN_RIGHT_ANGLE: "Right-angle",
        TOKEN_LEFT_PARENTHESIS: "Left-parenthesis",
        TOKEN_RIGHT_PARENTHESIS: "Right-parenthesis",
        TOKEN_COLON: "Colon",
        TOKEN_COMMA: "Comma"
    }
    return names.get(token)

class YsonToken(object):
    def __init__(self, value="", type=TOKEN_END_OF_STREAM):
        self._value = value
        self._type = type

    def get_value(self):
        return self._value

    def get_type(self):
        return self._type

    def _raise_error(self, message_end_of_stream, message_unexpected_token, token_type, value, expected_type):
        if token_type == TOKEN_END_OF_STREAM:
            raise YsonError(message_end_of_stream.format(expected_type))
        else:
            raise YsonError(message_unexpected_token.format(value, token_type_to_string(token_type), expected_type))

    def expect_type(self, type_or_types):
        token_type = self.get_type()
        expected_types = flatten(type_or_types)
        if token_type not in expected_types:
            if token_type == TOKEN_END_OF_STREAM:
                raise YsonError("Unexpected end of stream; expected types are {0}".format(expected_types))
            else:
                raise YsonError('Unexpected token "{0}" of type {1}; '
                                'expected types are {2}'.format(self._value,
                                                                token_type_to_string(token_type),
                                                                list(imap(token_type_to_string, expected_types))))

    def __str__(self):
        return str(self._value)
