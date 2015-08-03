#
# Basic protocol definitions for the beng-proxy translation server
# protocol.
#
# Author: Max Kellermann <mk@cm4all.com>
#

TRANSLATE_BEGIN = 1
TRANSLATE_END = 2
TRANSLATE_HOST = 3
TRANSLATE_URI = 4
TRANSLATE_STATUS = 5
TRANSLATE_PATH = 6
TRANSLATE_CONTENT_TYPE = 7
TRANSLATE_HTTP = 8
TRANSLATE_REDIRECT = 9
TRANSLATE_FILTER = 10
TRANSLATE_PROCESS = 11
TRANSLATE_SESSION = 12
TRANSLATE_PARAM = 13
TRANSLATE_USER = 14
TRANSLATE_LANGUAGE = 15
TRANSLATE_REMOTE_HOST = 16
TRANSLATE_PATH_INFO = 17
TRANSLATE_SITE = 18
TRANSLATE_CGI = 19
TRANSLATE_DOCUMENT_ROOT = 20
TRANSLATE_WIDGET_TYPE = 21
TRANSLATE_CONTAINER = 22
TRANSLATE_ADDRESS = 23
TRANSLATE_ADDRESS_STRING = 24
TRANSLATE_JAILCGI = 26
TRANSLATE_INTERPRETER = 27
TRANSLATE_ACTION = 28
TRANSLATE_SCRIPT_NAME = 29
TRANSLATE_AJP = 30
TRANSLATE_DOMAIN = 31
TRANSLATE_STATEFUL = 32
TRANSLATE_FASTCGI = 33
TRANSLATE_VIEW = 34
TRANSLATE_USER_AGENT = 35
TRANSLATE_MAX_AGE = 36
TRANSLATE_VARY = 37
TRANSLATE_QUERY_STRING = 38
TRANSLATE_PIPE = 39
TRANSLATE_BASE = 40
TRANSLATE_DELEGATE = 41
TRANSLATE_INVALIDATE = 42
TRANSLATE_LOCAL_ADDRESS = 43
TRANSLATE_LOCAL_ADDRESS_STRING = 44
TRANSLATE_APPEND = 45
TRANSLATE_DISCARD_SESSION = 46
TRANSLATE_SCHEME = 47
TRANSLATE_REQUEST_HEADER_FORWARD = 48
TRANSLATE_RESPONSE_HEADER_FORWARD = 49
TRANSLATE_DEFLATED = 50
TRANSLATE_GZIPPED = 51
TRANSLATE_PAIR = 52
TRANSLATE_UNTRUSTED = 53
TRANSLATE_BOUNCE = 54
TRANSLATE_ARGS = 55
TRANSLATE_WWW_AUTHENTICATE = 56
TRANSLATE_AUTHENTICATION_INFO = 57
TRANSLATE_AUTHORIZATION = 58
TRANSLATE_HEADER = 59
TRANSLATE_UNTRUSTED_PREFIX = 60
TRANSLATE_SECURE_COOKIE = 61
TRANSLATE_FILTER_4XX = 62
TRANSLATE_ERROR_DOCUMENT = 63
TRANSLATE_CHECK = 64
TRANSLATE_PREVIOUS = 65
TRANSLATE_WAS = 66
TRANSLATE_HOME = 67
TRANSLATE_REALM = 68
TRANSLATE_UNTRUSTED_SITE_SUFFIX = 69
TRANSLATE_TRANSPARENT = 70
TRANSLATE_STICKY = 71
TRANSLATE_DUMP_HEADERS = 72
TRANSLATE_COOKIE_HOST = 73
TRANSLATE_PROCESS_CSS = 74
TRANSLATE_PREFIX_CSS_CLASS = 75
TRANSLATE_FOCUS_WIDGET = 76
TRANSLATE_ANCHOR_ABSOLUTE = 77
TRANSLATE_PREFIX_XML_ID = 78
TRANSLATE_REGEX = 79
TRANSLATE_INVERSE_REGEX = 80
TRANSLATE_PROCESS_TEXT = 81
TRANSLATE_WIDGET_INFO = 82
TRANSLATE_EXPAND_PATH_INFO = 83
TRANSLATE_EXPAND_PATH = 84
TRANSLATE_COOKIE_DOMAIN = 85
TRANSLATE_LOCAL_URI = 86
TRANSLATE_AUTO_BASE = 87
TRANSLATE_UA_CLASS = 88
TRANSLATE_PROCESS_STYLE = 89
TRANSLATE_DIRECT_ADDRESSING = 90
TRANSLATE_SELF_CONTAINER = 91
TRANSLATE_GROUP_CONTAINER = 92
TRANSLATE_WIDGET_GROUP = 93
TRANSLATE_VALIDATE_MTIME = 94
TRANSLATE_NFS_SERVER = 95
TRANSLATE_NFS_EXPORT = 96
TRANSLATE_LHTTP_PATH = 97
TRANSLATE_LHTTP_URI = 98
TRANSLATE_EXPAND_LHTTP_URI = 99
TRANSLATE_LHTTP_HOST = 100
TRANSLATE_CONCURRENCY = 101
TRANSLATE_WANT_FULL_URI = 102
TRANSLATE_USER_NAMESPACE = 103
TRANSLATE_NETWORK_NAMESPACE = 104
TRANSLATE_EXPAND_APPEND = 105
TRANSLATE_EXPAND_PAIR = 106
TRANSLATE_PID_NAMESPACE = 107
TRANSLATE_PIVOT_ROOT = 108
TRANSLATE_MOUNT_PROC = 109
TRANSLATE_MOUNT_HOME = 110
TRANSLATE_MOUNT_TMP_TMPFS = 111
TRANSLATE_UTS_NAMESPACE = 112
TRANSLATE_BIND_MOUNT = 113
TRANSLATE_RLIMITS = 114
TRANSLATE_WANT = 115
TRANSLATE_UNSAFE_BASE = 116
TRANSLATE_EASY_BASE = 117
TRANSLATE_REGEX_TAIL = 118
TRANSLATE_REGEX_UNESCAPE = 119
TRANSLATE_FILE_NOT_FOUND = 120
TRANSLATE_CONTENT_TYPE_LOOKUP = 121
TRANSLATE_SUFFIX = 122
TRANSLATE_DIRECTORY_INDEX = 123
TRANSLATE_EXPIRES_RELATIVE = 124
TRANSLATE_EXPAND_REDIRECT = 125
TRANSLATE_EXPAND_SCRIPT_NAME = 126
TRANSLATE_TEST_PATH = 127
TRANSLATE_EXPAND_TEST_PATH = 128
TRANSLATE_REDIRECT_QUERY_STRING = 129
TRANSLATE_ENOTDIR = 130
TRANSLATE_STDERR_PATH = 131
TRANSLATE_COOKIE_PATH = 132
TRANSLATE_AUTH = 133
TRANSLATE_SETENV = 134
TRANSLATE_EXPAND_SETENV = 135
TRANSLATE_EXPAND_URI = 136
TRANSLATE_EXPAND_SITE = 137
TRANSLATE_REQUEST_HEADER = 138
TRANSLATE_EXPAND_REQUEST_HEADER = 139
TRANSLATE_AUTO_GZIPPED = 140
TRANSLATE_EXPAND_DOCUMENT_ROOT = 141
TRANSLATE_PROBE_PATH_SUFFIXES = 142
TRANSLATE_PROBE_SUFFIX = 143
TRANSLATE_AUTH_FILE = 144
TRANSLATE_EXPAND_AUTH_FILE = 145
TRANSLATE_APPEND_AUTH = 146
TRANSLATE_EXPAND_APPEND_AUTH = 147
TRANSLATE_LISTENER_TAG = 148
TRANSLATE_EXPAND_COOKIE_HOST = 149
TRANSLATE_EXPAND_BIND_MOUNT = 150
TRANSLATE_NON_BLOCKING = 151
TRANSLATE_READ_FILE = 152
TRANSLATE_EXPAND_READ_FILE = 153
TRANSLATE_EXPAND_HEADER = 154
TRANSLATE_REGEX_ON_HOST_URI = 155
TRANSLATE_SESSION_SITE = 156
TRANSLATE_IPC_NAMESPACE = 157
TRANSLATE_AUTO_DEFLATE = 158
TRANSLATE_EXPAND_HOME = 159
TRANSLATE_EXPAND_STDERR_PATH = 160
TRANSLATE_REGEX_ON_USER_URI = 161
TRANSLATE_AUTO_GZIP = 162
TRANSLATE_INTERNAL_REDIRECT = 163
TRANSLATE_LOGIN = 164
TRANSLATE_UID_GID = 165
TRANSLATE_PASSWORD = 166

TRANSLATE_PROXY = TRANSLATE_HTTP # deprecated
TRANSLATE_LHTTP_EXPAND_URI = TRANSLATE_EXPAND_LHTTP_URI # deprecated

HEADER_FORWARD_NO = 0
HEADER_FORWARD_YES = 1
HEADER_FORWARD_MANGLE = 2
HEADER_FORWARD_BOTH = 3

HEADER_GROUP_ALL = -1
HEADER_GROUP_IDENTITY = 0
HEADER_GROUP_CAPABILITIES = 1
HEADER_GROUP_COOKIE = 2
HEADER_GROUP_OTHER = 3
HEADER_GROUP_FORWARD = 4
HEADER_GROUP_CORS = 5
HEADER_GROUP_SECURE = 6
HEADER_GROUP_TRANSFORMATION = 7
