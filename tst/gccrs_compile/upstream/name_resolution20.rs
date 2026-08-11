
#![feature(decl_macro)]

pub mod foo {
    pub macro bar() {}
}

use foo::bar;

fn main() {
    bar!();
}
