//@ compile-fail: Use of an erased type outside of a function return
#![feature(derive_coerce_pointee)]
use std::marker::CoercePointee;
trait Bound { type Item; }
#[derive(CoercePointee)]
#[repr(transparent)]
struct Pointer<#[pointee] T: ?Sized + Bound<Item=impl Copy + ?Sized + use<T>>>(*const T);
fn main() {}
