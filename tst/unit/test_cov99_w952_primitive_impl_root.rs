//@ crate-type: lib
#![feature(rustc_attrs)]
#![rustc_coherence_is_core]
#![no_std]

impl u32 {
    pub fn cov99_w952_identity(self) -> Self {
        self
    }
}
