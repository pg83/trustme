//@ crate-type: lib
//@ compile-flags: --target $DIR/support/targets/cov99_w931_empty_compiler_options.toml --emit=metadata -Cpanic=abort
#![no_std]

pub fn identity(value: usize) -> usize {
    value
}
