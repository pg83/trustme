#![feature(lang_items, no_core)]
#![no_core]

#[lang = "pointee_sized"]
trait PointeeSized {}

#[lang = "meta_sized"]
trait MetaSized: PointeeSized {}

#[lang = "sized"]
trait Sized: MetaSized {}

#[repr(u8)]
enum Wide {
    V0,
    V1,
    V2,
    V3,
    V4,
    V5,
    V6,
    V7,
}

struct TupleValue(u32);

enum Payload {
    Value(u32),
}

#[no_mangle]
fn trustme_tuple_constructor() -> fn(u32) -> TupleValue {
    TupleValue
}

#[no_mangle]
fn trustme_enum_constructor() -> fn(u32) -> Payload {
    Payload::Value
}

unsafe extern "C" {
    fn trustme_switch_common() -> i32;
    fn trustme_switch_odd() -> i32;
}

#[no_mangle]
fn trustme_all_same_switch(value: Wide) -> i32 {
    unsafe {
        match value {
            Wide::V0
            | Wide::V1
            | Wide::V2
            | Wide::V3
            | Wide::V4
            | Wide::V5
            | Wide::V6
            | Wide::V7 => trustme_switch_common(),
        }
    }
}

#[no_mangle]
fn trustme_one_odd_switch(value: Wide) -> i32 {
    unsafe {
        match value {
            Wide::V3 => trustme_switch_odd(),
            _ => trustme_switch_common(),
        }
    }
}
