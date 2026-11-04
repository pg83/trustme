//@ aux-build: own_runtime_first.rs
//@ aux-build: own_runtime_second.rs
//@ compile-fail: Type mismatch
extern crate own_runtime_first;
extern crate own_runtime_second;
fn main() {
    let _: u8 = own_runtime_first::flag() && own_runtime_second::flag();
}
