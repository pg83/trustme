//@ crate-type: lib
#![allow(dead_code)]

extern crate core as renamed_core;

pub struct Unit;
pub struct Tuple<T>(pub T, pub i32);
pub struct Named<T> {
    pub value: T,
}

pub union Word {
    pub wide: u32,
    pub narrow: u8,
}

pub enum Enumeration<T> {
    Empty,
    Tuple(T),
    Named { value: i32 },
}

pub trait Associated {
    const CONSTANT: i32;
    type Output;
    fn method(&self) -> Self::Output;
}

impl Associated for Named<i32> {
    const CONSTANT: i32 = 5;
    type Output = i32;
    fn method(&self) -> i32 {
        self.value + Self::CONSTANT
    }
}

pub type Alias<T> = Named<T>;
pub static STATIC: Named<i32> = Named { value: 2 };
pub static mut MUTABLE: i32 = 4;
pub const CONSTANT: usize = 8;

pub const fn const_compute(value: usize) -> usize {
    value + CONSTANT
}

pub unsafe extern "C" fn unsafe_function(value: *const i32) -> i32 {
    unsafe { *value }
}

pub async fn asynchronous(value: i32) -> i32 {
    value
}

pub fn generic<T: Associated>(value: &T) -> T::Output {
    value.method()
}
