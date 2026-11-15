//@ compile-only
//@ compile-flags: --emit=dep-info -Cpanic=abort
#![feature(core_intrinsics)]
#![allow(internal_features)]

use core::any::TypeId;
use core::marker::PhantomData;

struct Deferred<T>(PhantomData<T>);

impl<T: 'static> Deferred<T> {
    const CALLBACK: fn() -> TypeId = if core::mem::size_of::<T>() == 2 {
        runtime_id::<T>
    } else {
        runtime_id::<u8>
    };
}

fn runtime_id<T: 'static>() -> TypeId {
    core::intrinsics::type_id::<T>()
}

fn invoke<T: 'static>() -> TypeId {
    (Deferred::<T>::CALLBACK)()
}

fn main() {
    let _ = invoke::<u16>();
}
