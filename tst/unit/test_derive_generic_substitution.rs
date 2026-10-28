//@ crate-type: lib
#![allow(dead_code)]

#[derive(Clone, Copy)]
struct FunctionPointer(fn(i32) -> i32);

#[derive(Clone)]
struct Fields<'a, T: Clone> {
    shared: &'a T,
    raw: *const T,
    array: [T; 2],
    function: fn(T) -> T,
    tuple: (T, T),
    object: &'a dyn Fn(T) -> T,
}

type Alias<'a, T> = Fields<'a, T>;

trait Convert<T> {
    type Output;
    const VALUE: T;
    fn convert(&self) -> Self::Output;
}

impl Convert<i32> for i32 {
    type Output = i32;
    const VALUE: i32 = 4;
    fn convert(&self) -> i32 {
        *self + Self::VALUE
    }
}

fn associated<T: Convert<i32>>(value: &T) -> T::Output {
    let _ = <T as Convert<i32>>::VALUE;
    <T as Convert<i32>>::convert(value)
}

fn bounded<T: Convert<i32, Output = i32>>(value: T) -> i32 {
    associated(&value)
}
