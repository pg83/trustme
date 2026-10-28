//@ crate-type: lib
#![allow(dead_code)]

trait First {
    fn first(&self) -> i32;
}

trait Second {
    fn second(&self) -> i32;
}

impl First for i32 {
    fn first(&self) -> i32 {
        *self
    }
}

impl Second for i32 {
    fn second(&self) -> i32 {
        *self + 1
    }
}

trait Both: First + Second {}
impl Both for i32 {}

fn object(value: &dyn First) -> i32 {
    value.first()
}

fn both(value: &dyn Both) -> i32 {
    value.first() + value.second()
}

fn opaque_first(value: i32) -> impl First {
    value
}

fn opaque_both(value: i32) -> impl First + Second {
    value
}

fn use_all(value: i32) -> i32 {
    object(&value) + both(&value) + opaque_first(value).first() + opaque_both(value).second()
}
