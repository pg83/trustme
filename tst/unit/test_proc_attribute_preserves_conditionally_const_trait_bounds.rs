//@ proc-macro-aux-build: conditional_const_identity.rs
#![feature(const_trait_impl)]

use conditional_const_identity::preserve;

#[const_trait]
trait Value {
    fn value() -> u16;
}

struct CompileTime;
impl const Value for CompileTime {
    fn value() -> u16 {
        419
    }
}

struct Runtime;
impl Value for Runtime {
    fn value() -> u16 {
        421
    }
}

#[preserve]
const fn read<T: [const] Value>() -> u16 {
    T::value()
}

const ANSWER: u16 = read::<CompileTime>();

fn main() {
    assert_eq!(ANSWER, 419);
    assert_eq!(read::<Runtime>(), 421);
}
