#![feature(no_core, lang_items)]
#![no_core]
#![no_main]
#![allow(internal_features)]

#[lang = "pointee_sized"]
trait PointeeSized {}
#[lang = "meta_sized"]
trait MetaSized: PointeeSized {}
#[lang = "sized"]
trait Sized: MetaSized {}

#[lang = "Option"]
enum Local<T> {
    #[lang = "Some"]
    Some(T),
    #[lang = "None"]
    None,
}

fn decode(value: Local<u16>) -> i32 {
    match value {
        Local::Some(29) => 0,
        Local::None => 1,
        _ => 2,
    }
}

#[no_mangle]
extern "C" fn main() -> i32 {
    match decode(Local::Some(29)) {
        0 => match decode(Local::None) {
            1 => 0,
            _ => 3,
        },
        _ => 4,
    }
}
