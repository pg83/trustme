//@ compile-fail: Mismatched type-generic count in
#![feature(trait_alias)]

trait Tail = Send;
trait Cycle = Cycle + Tail<()>;

fn main() {}
