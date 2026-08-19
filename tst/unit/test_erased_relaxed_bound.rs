// `impl ?Sized` names no trait at all: the only bound it carries says what the
// type is not.
#![feature(type_alias_impl_trait)]

type A = impl Sized;
#[define_opaque(A)]
fn f1() -> A {
    0u8
}

type B = impl ?Sized;
#[define_opaque(B)]
fn f2() -> &'static B {
    &[0u8]
}

type C = impl ?Sized + 'static;
#[define_opaque(C)]
fn f3() -> &'static C {
    &[0u8]
}

fn main() {
    let _ = f1();
    let _ = f2();
    let _ = f3();
}
