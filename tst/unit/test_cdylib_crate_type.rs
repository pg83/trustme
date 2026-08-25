//@ crate-type: cdylib

#[no_mangle]
pub extern "C" fn trustme_cdylib_answer() -> u32 {
    42
}
