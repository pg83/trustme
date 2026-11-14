//@ crate-type: lib
//@ proc-macro-aux-build: cov99_w310_module_identity.rs

#[cov99_w310_module_identity::identity]
pub mod kept {
    #[cfg(any())]
    pub fn removed() {}

    pub fn value() -> u8 {
        31
    }
}
