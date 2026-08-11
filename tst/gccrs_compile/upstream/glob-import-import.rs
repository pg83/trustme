// { dg-additional-options "-w" }
// TODO: this shouldn't warn


pub mod foo {
    pub struct S;
}

use foo::S;

mod bar {
    use super::*;

    pub const X: S = S;
}

pub const Y: S = bar::X;
