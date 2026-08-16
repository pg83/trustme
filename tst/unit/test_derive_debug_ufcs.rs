// The `Debug` derive used to call the formatter helpers as methods, so a trait
// in scope with a blanket impl and a method named `field`, `finish` or
// `debug_struct` captured the call. Calling them by path picks the inherent
// formatter method regardless of what else is in scope.
//
// Same shape as the upstream tests derives/derive-Debug-use-ufcs-struct.rs and
// derives/derive-Debug-use-ufcs-tuple.rs.
#![allow(warnings)]
use std::fmt::Error;

#[derive(Debug)]
pub struct Named {
    pub t: (),
    pub u: u8,
}

#[derive(Debug)]
pub struct Positional(pub (), pub u8);

#[derive(Debug)]
pub struct Unit;

#[derive(Debug)]
pub enum Choice {
    Bare,
    Tup(u8),
    Rec { a: u8 },
}

pub trait Access {
    fn field(&self, _: impl Sized, _: impl Sized) {
        panic!("got into Access::field");
    }
    fn finish(&self) -> Result<(), Error> {
        panic!("got into Access::finish");
    }
    fn debug_struct(&self, _: impl Sized, _: impl Sized) {
        panic!("got into Access::debug_struct");
    }
    fn debug_tuple(&self, _: impl Sized, _: impl Sized) {
        panic!("got into Access::debug_tuple");
    }
    fn write_str(&self, _: impl Sized) {
        panic!("got into Access::write_str");
    }
}

impl<T> Access for T {}

pub trait MutAccess {
    fn field(&mut self, _: impl Sized, _: impl Sized) {
        panic!("got into MutAccess::field");
    }
    fn finish(&mut self) -> Result<(), Error> {
        panic!("got into MutAccess::finish");
    }
    fn debug_struct(&mut self, _: impl Sized, _: impl Sized) {
        panic!("got into MutAccess::debug_struct");
    }
    fn debug_tuple(&mut self, _: impl Sized, _: impl Sized) {
        panic!("got into MutAccess::debug_tuple");
    }
    fn write_str(&mut self, _: impl Sized) {
        panic!("got into MutAccess::write_str");
    }
}

impl<T> MutAccess for T {}

fn main() {
    assert_eq!("Named { t: (), u: 1 }", format!("{:?}", Named { t: (), u: 1 }));
    assert_eq!("Positional((), 2)", format!("{:?}", Positional((), 2)));
    assert_eq!("Unit", format!("{:?}", Unit));

    assert_eq!("Bare", format!("{:?}", Choice::Bare));
    assert_eq!("Tup(3)", format!("{:?}", Choice::Tup(3)));
    assert_eq!("Rec { a: 4 }", format!("{:?}", Choice::Rec { a: 4 }));
}
