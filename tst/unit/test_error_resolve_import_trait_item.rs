//@ compile-fail: Unknown associated item missing of trait
#![feature(import_trait_associated_functions)]
trait T { fn present(); }
use T::missing;
fn main() {}
