//@ compile-fail: Too many type parameters passed to
#![feature(trait_alias)]

trait CovHolder<T: ?Sized> {}
trait CovAlias<T> = CovHolder<dyn Send<T>>;

fn trigger(_: &dyn CovAlias<u8>) {}

fn main() {}
