//@ input: support/targets/literal_line_break.toml
//@ compile-flags: --target=$DIR/support/targets/literal_line_break.toml
//@ compile-fail: newline in single-line string
fn main() { assert_eq!([5u8, 7, 11].len(), 3); }
