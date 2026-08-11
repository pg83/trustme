#![feature(lang_items, no_core)]
#![no_core]

#[lang = "pointee_sized"]
trait PointeeSized {}

#[lang = "meta_sized"]
trait MetaSized: PointeeSized {}

#[lang = "sized"]
trait Sized: MetaSized {}

#[lang = "copy"]
trait Copy {}

impl Copy for i32 {}

fn ordinary(value: i32) -> i32 {
    value
}

pub fn copies(value: i32) -> i32 {
    let first = value;
    let second = first;
    second
}

pub fn aggregate(first: i32, second: i32) -> i32 {
    let pair = (first, second);
    let selected = pair.0;
    selected
}

pub fn ordinary_call(value: i32) -> i32 {
    ordinary(value)
}
