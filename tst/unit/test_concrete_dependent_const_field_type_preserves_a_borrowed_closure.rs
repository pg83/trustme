#![feature(cfg_version, adt_const_params, generic_const_parameter_types)]
#![cfg_attr(version("1.96"), feature(const_param_ty_trait))]
#![cfg_attr(not(version("1.96")), feature(unsized_const_params))]
#![allow(incomplete_features)]

use std::marker::{ConstParamTy_, PhantomData};

struct Slot<T: ConstParamTy_, const VALUE: T>(PhantomData<T>);

struct Envelope {
    slot: Slot<usize, {
        let callback = &|| {
            let local = 29usize;
            local
        };
        std::mem::size_of_val(callback) + 31
    }>,
}

fn read<const VALUE: usize>(_: Slot<usize, VALUE>) -> usize {
    VALUE
}

fn main() {
    let envelope = Envelope { slot: Slot(PhantomData) };
    assert_eq!(read(envelope.slot), 31);
}
