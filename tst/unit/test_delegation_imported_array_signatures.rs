//@ aux-build: delegated_array_storage.rs
//@ aux-build: delegated_array_windows.rs
#![feature(fn_delegation)]
#![allow(incomplete_features)]
use delegated_array_windows::{Source, Windows};
struct Wrapped(Source);
impl Windows for Wrapped { reuse Windows::* { self.0 } }
fn main() {
    let wrapped = Wrapped(Source);
    assert_eq!(wrapped.rotate([2, 3, 5]), [3, 5, 2]);
    assert_eq!(wrapped.prefix([7, 11]), [8, 11]);
    assert_eq!(wrapped.prefix([]), []);
}
