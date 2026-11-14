//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
//@ proc-macro-aux-build: cov99_w311_trait_passthrough.rs

macro_rules! member {
    () => {
        fn value() -> u32 {
            311
        }
    };
}

#[cov99_w311_trait_passthrough::keep]
pub trait WithMacro {
    member!();
}
