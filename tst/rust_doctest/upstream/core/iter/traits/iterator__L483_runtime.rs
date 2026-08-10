// Extracted from library/core/src/iter/traits/iterator.rs:483
#![allow(unused)]
fn main() {
    #[cfg(windows)]
    fn os_str_to_utf16(s: &std::ffi::OsStr) -> Vec<u16> {
        use std::os::windows::ffi::OsStrExt;
        s.encode_wide().chain(std::iter::once(0)).collect()
    }
}
