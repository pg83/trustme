//@ compile-flags: --target $DIR/cov99_w929_compiler_opts_boolean.toml --emit=metadata -Cpanic=abort
//@ compile-fail: TOML type error: expected list, got boolean

fn main() {}
