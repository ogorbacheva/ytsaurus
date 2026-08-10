# HTTP proxy reference

## Command execution structure { #commands_structure }

When developing a library to work with {{product-name}}, you should understand the command execution structure.

Each executed command is a structure containing:

- Information about the user, allowing for authentication (in case of working over HTTP, it is most often a token).
- Information about the input and output data [format]({{ docs_root }}/core/user-guide/reference/storage/formats) (represented as a YSON string with attributes).
- Information about the command parameters (represented as a YSON dict).
- Input (byte) data stream.
- Output (byte) data stream.

Below are the formal coding specifications for each item in the HTTP proxy, which include more features for interpreting HTTP queries (partially to support compatibility with such HTTP clients as web browsers). To work correctly with {{product-name}}, you need to support a smaller set of features, namely:

- Specify `X-YT-Header-Format` that defines the format for the `X-YT-Input-Format`, `X-YT-Output-Format`, and `X-YT-Parameters` headers (the header value is YSON).
- Specify `X-YT-Input-Format` and `X-YT-Output-Format` encoded according to `X-YT-Header-Format` and defining the input and output data stream formats.
- Pass all command parameters in the `X-YT-Parameters` header encoded according to `X-YT-Header-Format`.
- Consider all HTTP responses 5xx as a transport-layer error. You must distinguish code 503 that is a clear signal of temporary unavailability from code 500 that signals an error on the {{product-name}} side. In the first case, you can repeat the query.
- In all other responses (2xx, 4xx) about the successful execution of the command, judge by `X-YT-Error` (preferably) or `X-YT-Response-Code` (extracted from the `X-YT-Error` error code) and `X-YT-Response-Message` (extracted from the `X-YT-Error` error troubleshooting) headers/trailers.

## Selecting an HTTP method for a command { #http_method }

To select an HTTP method for a command, just use the following algorithm:

1. If the command has an input data stream, then PUT.
2. If the command is mutating, then POST.
3. Otherwise GET.


## Data formats for operations { #formats }

Interpreting input and output byte streams (with a data type other than binary) is determined by the [data format]({{ docs_root }}/core/user-guide/reference/storage/formats). A description of the format used is a YSON string possibly with additional attributes. A standard way to describe the formats used are the `X-YT-Input-Format` and `X-YT-Output-Format` headers. For better compatibility with HTTP libraries, the `Accept` and `Content-Type` headers are partially supported. Below is a detailed description of the format definition rules.

### Specifying an input data format { #set_input_format }

An input data format is determined using the following rules. Each successive rule overlaps the previous one.

1. If the `Content-Type` header is specified and the specified MIME type is in the correspondence table, the format is selected from the table.
2. If the `X-YT-Input-Format` header is specified, the header content is interpreted as a JSON-encoded YSON string with attributes and it is used as an input format description.
3. If neither variant 1 nor variant 2 is successful, YSON is used.

### Specifying an output data format { #set_output_format }

An output data format is determined using the following rules. Each successive rule overlaps the previous one.

1. If the `Accept` header is specified, the best MIME type from the table corresponding to the Accept header is selected and then the format is taken from the table. Content-Type is equal to the matching MIME type.
2. If the `X-YT-Output-Format` header is specified, the header content is interpreted as a JSON-encoded YSON string with attributes and it is used as an output format description. Content-Type is equal to `application/octet-stream`.
3. If neither variant 1 nor variant 2 is successful, Pretty YSON is used. Content-Type is equal to `text/plain`.

## HTTP return codes { #return_codes }

If the web server realizes that there is an error before any data is sent to the client, return code 4xx or 5xx indicates an error. If the data started to be delivered to the client, the return code will be (`Accepted`) and there will be a {{product-name}} return code in the `X-YT-Response-Code` trailer header. A non-zero `X-YT-Response-Code` indicates an error. In this case, an error message (as a JSON string) is specified in the `X-YT-Response-Message` trailer header.

<small>Table 1 — Return codes</small>

| Return code | Description |
| ------------ | ------------------------------------------------------------ |
| 200 | The command was successfully completed |
| 202 | The command is executed, the response body will be sent over an HTTP stream, and internal return codes are written in the trailer headers |
| 307 | Redirecting heavy queries from light to heavy proxies |
| 400 | The command was executed, but an error was returned (detailed JSON-encoded error in the body) |
| 401 | Unauthenticated query |
| 404 | Unknown command |
| 405 | Incorrect HTTP method |
| 406 | Incorrect format in Accept |
| 415 | Incorrect format in Accept-Encoding |
| 429 | The limit on the number of queries from a user was exceeded |
| 500 | Error on the proxy side |
| 503 | Service unavailable. The query must be repeated later |

## Query debugging { #debugging }

{% note info "Note" %}

All queries support optional debug headers. Processing them is not mandatory, but recommended.

{% endnote %}

In the query:

1. Generate guid and set it in the `X-YT-Correlation-Id` header. This header helps you find the query by log even if the response to it did not come to the client.

In the response:

1. `X-YT-Request-Id`: ID of the query generated by the proxy. You need it to find the query in the {{product-name}} proxy log.
2. `X-YT-Proxy`: Hostname of the proxy from which the response came. It is important when the query passes through a balancer.

## Advanced features { #additional }

### Compression { #compression }

You can use compression when transmitting data via an HTTP proxy. The proxy selects a codec for incoming data based on the `Content-Encoding` header and for outgoing data based on the `Accept-Encoding` header. Possible codecs are listed in the table.

{{ core-user-guide-reference-proxy-http-reference-md-audience-1 }}

### Parameters { #parameters }

Each command has a set of additional parameters represented as a YSON dict. A standard method to pass parameters is the `X-YT-Parameters` header interpreted as a JSON-encoded YSON dict.

### List of heavy proxies (/hosts) { #hosts }

All {{product-name}} commands are classified into two groups: light and heavy. Heavy commands are associated with a large I/O and, consequently, a network load. To isolate this load, light (controlling) commands are separated from the heavy ones. When you try to execute a heavy command, light proxies return code 503. The balancing of heavy commands among heavy proxies is performed by the client.

Before you execute a heavy command, you need to query `/hosts` and get a list of proxies ordered by load. The load is estimated based on the current CPU and network load, as well as some planned future load on the proxies. The very first proxy in the resulting list is the least loaded, and that's the one you want to use in simple cases (= in 80% of cases).

{{ core-user-guide-reference-proxy-http-reference-md-audience-2 }}

### Available API versions (/api) { #api }

The HTTP API is versioned (as you can see from the `/v4` prefix in the examples [from here]({{ docs_root }}/core/user-guide/reference/proxy/http)). The API version is changed if there are backward-incompatible changes to the set of supported commands or to the semantics of any of the existing commands. Adding new commands does not usually change the API version. The HTTP proxy supports the two latest versions of the API.

A list of supported API versions can be obtained from the URL `/api`.

Getting a list of available APIs:

```bash
$ curl -v -X GET "http://$YT_PROXY/api"
> GET /api HTTP/1.1
< HTTP/1.1 200 OK
["v3","v4"]
```

## The table of correspondence of MIME types and YT formats { #mime_to_yt_format }

 The correspondence of MIME types to YT formats is represented in the table.

| **MIME type** | **YT format** |
| ----------------------------------- | ----------------------------------- |
| application/json | json |
| application/x-yt-yson-binary | <format=binary>yson |
| application/x-yt-yson-text | <format=text>yson |
| application/x-yt-yson-pretty | <format=pretty>yson |
| application/x-yamr-delimited | <lenval=false;has_subkey=false>yamr |
| application/x-yamr-lenval | <lenval=true;has_subkey=false>yamr |
| application/x-yamr-subkey-delimited | <lenval=false;has_subkey=true>yamr |
| application/x-yamr-subkey-lenval | <lenval=true;has_subkey=true>yamr |
| text/tab-separated-values | dsv |
| text/x-tskv | <line_prefix=tskv>dsv |

## Framing { #framing }

Some commands may use a special protocol on top of the usual HTTP.
If the client specifies the `X-YT-Accept-Framing: 1` header, the proxy can respond with the `X-YT-Framing: 1` header.
In this case, the response body will consist of entries of the `<tag> <header> <frame>` type.

Frame types:

| **Name** | **Tag** | **Header** | **Frame** | **Comment** |
|--------------|---------|------------------------------------------------|-------------|------------------|
| Data | `0x01` | 4-byte little-endian number — frame size | frame body |                  |
| Keep-alive | `0x02` | none | none | "the data is being prepared, please wait" |

{% if version > '1' %}

## The table of correspondence of commands and HTTP methods { #command_to_http_method }

A list of supported commands in a specific version can be obtained from the URL `/api/vN`.

Each command is annotated with four criteria:

- Input data type (`input_type`): none (`none`), structured (`structured`), tabular (`tabular`), or binary (`binary`).
- Output data type (`output_type`): same as the input data type.
- Whether the command is mutating or not (`is_volatile`).
- Whether the command is heavy or not (`is_heavy`).

A mutating command is one that changes something in the replicated meta-state (aka: master server state; aka: Cypress).
A heavy command is one that processes large amounts of data on the client.

Below is a list of supported commands in version 0.17.

| **Name** | **Input data** | **Output data** | **Mutating?** | **Heavy?** | **Repeatable?** | **HTTP method** |
| -------------------------------------------------------- | ------------------ | ------------------- | --------------- | ------------ | ---------------- | -------------- |
| [start_tx]({{ docs_root }}/core/user-guide/reference/api/commands#starttx) |  | Structured | Yes | No | Yes | POST |
| [ping_tx]({{ docs_root }}/core/user-guide/reference/api/commands#pingtx) |  |  | Yes | No | No | POST |
| [commit_tx]({{ docs_root }}/core/user-guide/reference/api/commands#committx) |  |  | Yes | No | Yes | POST |
| [abort_tx]({{ docs_root }}/core/user-guide/reference/api/commands#aborttx) |  |  | Yes | No | Yes | POST |
| [create]({{ docs_root }}/core/user-guide/reference/api/commands#create) |  | Structured | Yes | No | Yes | POST |
| [remove]({{ docs_root }}/core/user-guide/reference/api/commands#remove) |  |  | Yes | No | Yes | POST |
| [set]({{ docs_root }}/core/user-guide/reference/api/commands#set) | Structured |  | Yes | No | Yes | PUT |
| [get]({{ docs_root }}/core/user-guide/reference/api/commands#get) |  | Structured | No | No | No | GET |
| [list]({{ docs_root }}/core/user-guide/reference/api/commands#list) |  | Structured | No | No | No | GET |
| [lock]({{ docs_root }}/core/user-guide/reference/api/commands#lock) |  | Structured | Yes | No | Yes | POST |
| [copy]({{ docs_root }}/core/user-guide/reference/api/commands#copy) |  | Structured | Yes | No | Yes | POST |
| [move]({{ docs_root }}/core/user-guide/reference/api/commands#move) |  | Structured | Yes | No | Yes | POST |
| [link]({{ docs_root }}/core/user-guide/reference/api/commands#link) |  | Structured | Yes | No | Yes | POST |
| [exists]({{ docs_root }}/core/user-guide/reference/api/commands#exists) |  | Structured | No | No | No | GET |
| [write_file]({{ docs_root }}/core/user-guide/reference/api/commands#writefile) | Binary | Structured | Yes | Yes | No | PUT |
| [read_file]({{ docs_root }}/core/user-guide/reference/api/commands#readfile) |  | Binary | No | Yes | No | GET |
| [write_table]({{ docs_root }}/core/user-guide/reference/api/commands#writetable) | Tabular |  | Yes | Yes | No | PUT |
| [read_table]({{ docs_root }}/core/user-guide/reference/api/commands#readtable) |  | Tabular | No | Yes | No | GET |
| [write_journal]({{ docs_root }}/core/user-guide/reference/api/commands#writejournal) | Tabular |  | Yes | Yes | No | PUT |
| [read_journal]({{ docs_root }}/core/user-guide/reference/api/commands#readjournal) |  | Tabular | No | Yes | No | GET |
| [select_rows]({{ docs_root }}/core/user-guide/reference/api/commands#select_rows) (0.17+) |  | Tabular | No | Yes | No | GET |
| [merge]({{ docs_root }}/core/user-guide/reference/api/commands#merge) |  | Structured | Yes | No | Yes | POST |
| [erase]({{ docs_root }}/core/user-guide/reference/api/commands#erase) |  | Structured | Yes | No | Yes | POST |
| [map]({{ docs_root }}/core/user-guide/reference/api/commands#map) |  | Structured | Yes | No | Yes | POST |
| [reduce]({{ docs_root }}/core/user-guide/reference/api/commands#reduce) |  | Structured | Yes | No | Yes | POST |
| [map_reduce]({{ docs_root }}/core/user-guide/reference/api/commands#map_reduce) |  | Structured | Yes | No | Yes | POST |
| [sort]({{ docs_root }}/core/user-guide/reference/api/commands#sort) |  | Structured | Yes | No | Yes | POST |
| [abort_op]({{ docs_root }}/core/user-guide/reference/api/commands#abortop) |  |  | Yes | No | No | POST |



```TODO:   "concat",   "add_member",   "remove_member",   "check_permission",      "dump_job_context",   "insert_rows",   "delete_rows",   "lookup_rows",   "select_rows",    "mount_table",   "unmount_table",   "remount_table",   "reshard_table",    "parse_ypath",   "get_version",    "remote_copy",     "suspend_op",   "resume_op",`

{% endif %}
