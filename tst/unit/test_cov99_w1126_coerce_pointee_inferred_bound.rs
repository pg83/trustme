//@ compile-only
//@ compile-flags: -Zstop-after=expand -Zdump-ast
#![feature(derive_coerce_pointee)]

trait Bound<A> {}

#[derive(CoercePointee)]
#[repr(transparent)]
struct Ptr<T: ?Sized + Bound<_>>(*const T);
