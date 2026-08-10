// Extracted from library/std/src/ffi/os_str.rs:55
#![allow(unused)]
fn main() {
    use std::ffi::{OsStr, OsString};

    fn concat_os_strings(a: &OsStr, b: &OsStr) -> OsString {
        let mut ret = OsString::with_capacity(a.len() + b.len()); // This will allocate
        ret.push(a); // This will not allocate further
        ret.push(b); // This will not allocate further
        ret
    }
}
