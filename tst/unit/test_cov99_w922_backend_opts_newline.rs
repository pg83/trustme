//@ crate-type: lib
//@ compile-flags: --target $DIR/inputs/cov99_w922_backend_opts_newline.toml
#![no_std]

#[no_mangle]
pub fn cov99_w922_identity(value: u32) -> u32 {
    value
}
