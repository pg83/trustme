//@ crate-type: lib
#![feature(no_core, cfg_target_has_atomic)]
#![no_core]

#[cfg(target_has_atomic_load_store)]
macro_rules! atomic_int {
    () => {};
}

#[cfg(target_has_atomic_load_store = "8")]
atomic_int! {}
