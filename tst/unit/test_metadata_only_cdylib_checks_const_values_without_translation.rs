//@ crate-type: cdylib
//@ default-compile-flags: --emit=metadata

const fn combine(value: u16) -> u16 {
    (value << 3) ^ 0x85
}

const _: () = assert!(combine(17) == 13);

#[no_mangle]
pub extern "C" fn transform(value: u16) -> u16 {
    combine(value)
}
