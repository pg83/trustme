//@ compile-flags: -Clink-arg=-lc -Coverflow-checks=off
#![feature(lang_items, no_core)]
#![no_core]
#![no_main]

#[lang = "pointee_sized"] trait PointeeSized {}
#[lang = "meta_sized"] trait MetaSized: PointeeSized {}
#[lang = "sized"] trait Sized: MetaSized {}
#[lang = "copy"] trait Copy {}
impl Copy for i32 {}
#[lang = "drop_in_place"] unsafe fn drop_in_place<T: PointeeSized>(_: *mut T) {}
#[lang = "add"]
trait Add<Rhs = Self> {
    type Output;
    fn add(self, rhs: Rhs) -> Self::Output;
}

impl Add for i32 {
    type Output = i32;
    fn add(self, rhs: i32) -> i32 { self + rhs }
}

struct Amount(i32);
impl Add for Amount {
    type Output = i32;
    fn add(self, rhs: Amount) -> i32 { self.0 + rhs.0 }
}

fn combine<T: Add<Output = i32>>(left: T, right: T) -> i32 { left + right }

#[no_mangle]
extern "C-unwind" fn main() -> i32 {
    match (combine(17i32, 25i32), combine(Amount(5), Amount(8)), <i32 as Add>::add(31, 16)) {
        (42, 13, 47) => 0,
        _ => 1,
    }
}
