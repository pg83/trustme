//@ input: support/targets/basic_line_break.toml
//@ compile-flags: --target=$DIR/support/targets/basic_line_break.toml
//@ compile-fail: newline in single-line string
fn main() { assert_eq!([17u8, 19, 23].len(), 3); }
