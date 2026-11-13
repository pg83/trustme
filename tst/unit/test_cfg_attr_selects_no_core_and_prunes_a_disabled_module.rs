//@ crate-type: lib
#![feature(no_core)]
#![cfg_attr(all(), no_std, no_core)]

#[cfg_attr(all(), cfg(any()))]
mod disabled {
    compile_error!("a disabled module was expanded");
}
