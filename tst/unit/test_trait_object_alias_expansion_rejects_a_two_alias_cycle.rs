//@ compile-fail: Recursive trait alias in trait object
#![feature(trait_alias)]

trait First = Second;
trait Second = First;

fn rejected(_: &dyn First) {}

fn main() {}
