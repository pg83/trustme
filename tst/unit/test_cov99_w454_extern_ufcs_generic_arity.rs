//@ crate-type: lib
//@ compile-flags: --emit=metadata
//@ compile-fail: Failed to find impl with 'Out'
#![feature(extern_types)]

extern "C" { type Opaque; }

trait TypePair { type Out; }
trait ValuePair { type Out; }
impl<T, A, B> TypePair for (T, T, A, B) { type Out = (); }
impl<T, A, B> ValuePair for (A, B, T, T) { type Out = (); }

type Invalid<const N: usize> = <(
    *const Opaque<u8>, *const Opaque,
    *const Opaque<{ N }>, *const Opaque,
)>::Out;
