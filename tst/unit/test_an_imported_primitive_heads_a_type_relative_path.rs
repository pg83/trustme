// unsafe-libyaml (fluent-bundle's tests build it through serde_yaml) has
//     mod libc { pub use core::primitive::{i32 as c_int, u8 as c_uchar, ..}; }
// and writes `libc::c_int::MAX`. A path segment that names a primitive type
// through an import makes the rest of the path relative to that type, as
// `i32::MAX` is: upstream resolution stops at the type and resolves `MAX`
// type-relatively (`<i32>::MAX`). Here the walk through the path's modules only
// knew type aliases, structs and enums, and reported the primitive as a
// "non-namespace item".
mod libc {
    pub use core::primitive::{i32 as c_int, u8 as c_uchar};
}
fn main() {
    assert_eq!(libc::c_int::MAX, i32::MAX);
    assert_eq!(crate::libc::c_uchar::MIN, 0);
    let x: libc::c_int = libc::c_int::from(3u8);
    assert_eq!(x.count_ones(), 2);
}
