// Extracted from library/core/src/marker/variance.rs:256
#![allow(unused)]
#![feature(phantom_variance_markers)]
fn main() {
    
    use core::marker::{PhantomCovariant, variance};
    
    struct BoundFn<F, P, R>
    where
        F: Fn(P) -> R,
    {
        function: F,
        parameter: P,
        return_value: PhantomCovariant<R>,
    }
    
    let bound_fn = BoundFn {
        function: core::convert::identity,
        parameter: 5u8,
        return_value: variance(),
    };
}
