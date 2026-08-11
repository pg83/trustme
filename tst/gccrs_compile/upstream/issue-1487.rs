// { dg-options "-w" }

#![feature(lang_items)]

trait Printable {
    fn print(&self);
}

struct Foo;

impl Printable for Foo {
    fn print(&self) {}
}

fn take_printable(_: impl Printable) {}
