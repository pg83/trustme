// Extracted from library/core/src/mem/mod.rs:974
#![allow(unused)]
#![feature(mem_copy_fn)]
fn main() {
    use core::mem::copy;
    let result_from_ffi_function: Result<(), &i32> = Err(&1);
    let result_copied: Result<(), i32> = result_from_ffi_function.map_err(copy);
}
