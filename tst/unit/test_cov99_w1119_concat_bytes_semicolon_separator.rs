//@ compile-fail: Unexpected token TOK_SEMICOLON, expected one of TOK_COMMA or TOK_EOF

#![feature(concat_bytes)]

fn main() {
    let _ = concat_bytes!(b"w1119";);
}
