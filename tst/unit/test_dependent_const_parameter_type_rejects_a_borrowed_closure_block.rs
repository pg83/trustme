//@ compile-fail: overly complex generic constant
#![feature(adt_const_params, generic_const_parameter_types, generic_const_exprs)]
#![allow(incomplete_features)]

struct Marker<T, const VALUE: T>(std::marker::PhantomData<T>);

fn inspect<T: Copy, const VALUE: T>() {
    let _: Marker<T, {
        let callback = &|| {
            let local = 19usize;
            local
        };
        let _ = callback;
        VALUE
    }>;
}

fn main() {}
