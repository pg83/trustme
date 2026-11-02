//@ input: support/targets/literal_six_quotes_at_eof.toml
//@ input: support/targets/basic_six_quotes_at_eof.toml
//@ input: support/targets/literal_two_quotes_at_eof.toml
//@ input: support/targets/basic_two_quotes_at_eof.toml
//@ reject-flags: --target=$DIR/support/targets/literal_six_quotes_at_eof.toml --print cfg => Unexpected EOF in single-quoted string || error loading target specification
//@ reject-flags: --target=$DIR/support/targets/basic_six_quotes_at_eof.toml --print cfg => Unexpected EOF in double-quoted string || error loading target specification
//@ reject-flags: --target=$DIR/support/targets/literal_two_quotes_at_eof.toml --print cfg => Unexpected EOF in triple-quoted string || error loading target specification
//@ reject-flags: --target=$DIR/support/targets/basic_two_quotes_at_eof.toml --print cfg => Unexpected EOF in triple-quoted string || error loading target specification
//@ compile-variant: -O
fn main() { assert_eq!([29u16, 31, 37].iter().copied().sum::<u16>(), 97); }
