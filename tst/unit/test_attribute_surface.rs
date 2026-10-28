#![allow(dead_code)]
#![warn(unused_imports)]
#![doc = "crate documentation"]

use std::fmt::Debug;

#[derive(Copy, Clone, Debug, Default, Eq, PartialEq, Ord, PartialOrd, Hash)]
#[repr(C)]
#[doc(hidden)]
struct Record {
    value: i32,
}

#[derive(Clone, Debug)]
#[repr(align(16))]
struct Aligned(Vec<i32>);

#[cfg(any(unix, target_os = "linux"))]
#[cfg_attr(debug_assertions, inline(always))]
#[cfg_attr(not(debug_assertions), inline(never))]
#[cold]
#[track_caller]
fn compute(value: i32) -> i32 {
    value + 1
}

#[no_mangle]
#[used]
#[link_section = ".data"]
static mut EXPORTED: i32 = 3;

#[export_name = "renamed"]
#[inline(never)]
pub extern "C" fn exported_function() -> i32 {
    compute(unsafe { EXPORTED })
}

#[macro_export]
#[doc(hidden)]
macro_rules! exported_macro {
    ($value:expr) => {
        $value + 1
    };
}

#[must_use]
#[deprecated(note = "old API")]
fn old_api() -> i32 {
    1
}

#[cfg(FALSE)]
fn removed() {}

#[path = "aux/resolution.rs"]
mod resolution;

#[cfg(test)]
mod tests {
    #[test]
    fn attribute_unit() {
        assert_eq!(super::compute(1), 2);
    }
}
