#!/usr/bin/env python3
"""Pack httpd-page-src.html into the served httpd-page.html.

The recovery web server sends the embedded page verbatim, so the artifact
carries the complete HTTP response: a fixed header block whose
Content-Length matches the gzip body that follows it.
"""
import gzip
import pathlib

src = pathlib.Path(__file__).parent / "httpd-page-src.html"
dst = pathlib.Path(__file__).parent / "httpd-page.html"

body = gzip.compress(src.read_bytes(), 9, mtime=0)
head = ("HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Encoding: gzip\r\n"
        "Connection: close\r\n"
        "Content-Length: %d\r\n\r\n" % len(body))

dst.write_bytes(head.encode("ascii") + body)
print("packed %s: %d + %d bytes" % (dst, len(head), len(body)))
