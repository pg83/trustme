#![feature(adt_const_params, generic_const_parameter_types)]
#![allow(incomplete_features)]

use std::marker::{ConstParamTy_, PhantomData};

struct Slot<T: ConstParamTy_, const VALUE: T>(PhantomData<T>);

fn read<const VALUE: usize>(_: Slot<usize, VALUE>) -> usize {
    VALUE
}

fn main() {
    let slot = Slot::<usize, {
        let borrowed = &|| {
            let local = 37usize;
            local
        };
        std::mem::size_of_val(borrowed) + 41
    }>(PhantomData);
    assert_eq!(read(slot), 41);
}
