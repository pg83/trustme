#![feature(no_core, lang_items)]
#![no_core]
#![no_main]
//@ compile-fail: Type mismatch between i32 and u8
#[lang = "pointee_sized"]
trait PointeeSized {}
#[lang = "meta_sized"]
trait MetaSized: PointeeSized {}
#[lang = "sized"]
trait Sized: MetaSized {}
#[lang = "copy"]
trait Copy {}
impl Copy for u8 {}
impl Copy for i32 {}

#[lang = "neg"]
trait Neg {
    type Output;
    fn neg(self) -> Self::Output;
}

impl Neg for u8 {
    type Output = i32;

    fn neg(self) -> i32 {
        29
    }
}

#[no_mangle]
extern "C" fn main() -> i32 {
    let input = 5u8;
    let result: i32 = -input;
    match result {
        29 => 0,
        _ => 1,
    }
}
