//@ compile-fail: anonymous constants referencing generics are not yet supported
#![feature(adt_const_params, generic_const_parameter_types, generic_const_exprs)]
#![allow(incomplete_features)]

struct Marker<T, const VALUE: T>(std::marker::PhantomData<T>);

struct Envelope<T: Copy, const VALUE: T> {
    marker: Marker<T, {
        let callback = &|| {
            let local = 23usize;
            local
        };
        let _ = callback;
        VALUE
    }>,
}

fn main() {}
