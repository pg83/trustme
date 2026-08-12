//@ compile-fail: const trait call requires a const impl
#![feature(const_trait_impl)]

#[const_trait]
trait Value {
    fn value() -> u32;
}

struct Runtime;

impl Value for Runtime {
    fn value() -> u32 {
        7
    }
}

const fn read<T: [const] Value>() -> u32 {
    T::value()
}

const BAD: u32 = read::<Runtime>();

fn main() {
    let _ = BAD;
}
