// A method call resolved in an outer scope still has to find an inherent
// method, so the index of them is built before the passes that resolve a UFCS
// path run.
//@ crate-type: lib
//@ compile-flags: --emit=metadata
#![feature(adt_const_params, unsized_const_params, generic_const_exprs)]
#![allow(incomplete_features)]

pub struct Changes<const CHANGES: &'static [&'static str]>
where
    [(); CHANGES.len()]:,
{
    changes: [usize; CHANGES.len()],
}

impl<const CHANGES: &'static [&'static str]> Changes<CHANGES>
where
    [(); CHANGES.len()]:,
{
    pub const fn new() -> Self {
        Self { changes: [0; CHANGES.len()] }
    }
}
