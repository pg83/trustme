#![feature(const_trait_impl)]

#[const_trait]
trait Scale {
    fn scale() -> u32;
}

#[const_trait]
trait Value: [const] Scale {
    fn value() -> u32
    where
        Self: [const] Scale,
    {
        Self::scale() * 2
    }
}

struct Runtime;

impl Scale for Runtime {
    fn scale() -> u32 {
        3
    }
}

impl Value for Runtime {}

struct CompileTime;

impl const Scale for CompileTime {
    fn scale() -> u32 {
        21
    }
}

impl const Value for CompileTime {}

const fn read<T: [const] Value>() -> u32 {
    T::value()
}

const ANSWER: u32 = read::<CompileTime>();

fn main() {
    assert_eq!(ANSWER, 42);
    assert_eq!(read::<Runtime>(), 6);
}
