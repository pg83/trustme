// A module that both declares a submodule and glob-re-exports through another
// glob (libc's `new/` layout) tripped the resolve recursion guard.
mod inner {
    pub mod thing { pub struct T(pub u32); }
    pub use self::thing::*;   // glob within the module
    pub use crate::inner::thing::T as T2;
}
pub use inner::*;
fn main() {
    let a = inner::T(5);
    let b: inner::T2 = inner::T2(7);
    assert_eq!(a.0 + b.0, 12);
}
