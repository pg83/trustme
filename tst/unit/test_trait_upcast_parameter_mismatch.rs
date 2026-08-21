//@ compile-fail: mismatch

trait Parent<T> {}
trait Child: Parent<u32> {}

fn reject(value: &dyn Child) {
    let _ = value as &dyn Parent<i32>;
}

fn main() {}
