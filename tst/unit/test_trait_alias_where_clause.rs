#![feature(trait_alias)]

trait Base {}

trait WithWhere<T> = Base where T: Base;
trait BareWhere<T> = where T: Base;

fn main() {}
