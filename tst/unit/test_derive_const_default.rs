#![feature(const_default)]
#![feature(const_trait_impl)]
#![feature(derive_const)]

#[derive_const(Default)]
struct Value;

#[derive_const(Default)]
struct Wrapper<T>(T);

const VALUE: Value = Value::default();
const WRAPPER: Wrapper<Value> = Wrapper::default();

fn main() {
    let _ = VALUE;
    let _ = WRAPPER;
}
