//@ crate-type: lib

trait Trait {
    const ASSOC: usize;
}

impl<T> Trait for T {
    const ASSOC: usize = 10;
}

fn accepted<T>(value: usize) {
    match value {
        <T as Trait>::ASSOC => (),
        _ => (),
    }
}

fn main() {}
