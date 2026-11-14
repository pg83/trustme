//@ compile-fail: Unexpected token TOK_EOF
//@ compile-flags: --emit=metadata -Cpanic=abort

macro_rules! capture {
    ($attribute:meta $operator:tt $value:tt) => {};
}

capture!(key = destination <- value);

fn main() {}
