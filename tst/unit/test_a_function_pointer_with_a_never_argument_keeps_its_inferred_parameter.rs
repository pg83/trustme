#![feature(never_type)]

fn sink<T>(_: !, _: T) {}

fn make<T>() -> fn(!, T) {
    sink::<T>
}

fn retain(_: fn(!, u8)) {}

fn main() {
    let pointer = make();
    retain(pointer);
}
