//@ compile-fail: only public within the crate and cannot be re-exported outside
//@ crate-type: lib

macro_rules! private_macro {
    () => {};
}

pub use private_macro;
