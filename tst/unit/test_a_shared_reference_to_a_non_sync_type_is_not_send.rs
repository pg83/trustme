// core writes `unsafe impl<T: Sync + ?Sized> Send for &T`. Upstream then
// assembles no structural auto-trait candidate for `&T`: an impl of the
// auto trait for the type's head replaces it, whether or not its bounds
// hold, so `&Cell<u8>` is not `Send` although `Cell<u8>` is.
//@ compile-fail: Failed to find an impl
use std::cell::Cell;

fn need<T: ?Sized + Send>() {}

fn main() {
    need::<Cell<u8>>();
    need::<&u8>();
    need::<&Cell<u8>>();
}
