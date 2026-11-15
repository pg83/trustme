//@ compile-fail: Unexpected token TOK_SEMICOLON, expected one of TOK_COMMA or TOK_EOF

fn main() {
    let _ = concat!("w1119";);
}
