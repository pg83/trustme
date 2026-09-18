/* Two statics past the 64KiB threshold that turns a static into an assembler
   `.incbin` of a companion blob file. The second one only reads back correctly
   if its offset into that blob is honoured. */
#![feature(lang_items, no_core)]
#![no_core]
#![no_main]

#[lang = "pointee_sized"]
trait PointeeSized {}

#[lang = "meta_sized"]
trait MetaSized: PointeeSized {}

#[lang = "sized"]
trait Sized: MetaSized {}

#[lang = "copy"]
trait Copy {}

static TABLE_A: [u8; 70000] = [0xA5; 70000];
static TABLE_B: [u8; 70000] = [0x5A; 70000];

#[no_mangle]
extern "C-unwind" fn main() -> i32 {
    let a = &raw const TABLE_A as *const u8;
    let b = &raw const TABLE_B as *const u8;
    unsafe {
        if *a != 0xA5 {
            return 1;
        }
        if *(((a as usize) + 69999) as *const u8) != 0xA5 {
            return 2;
        }
        if *b != 0x5A {
            return 3;
        }
        if *(((b as usize) + 69999) as *const u8) != 0x5A {
            return 4;
        }
    }
    0
}
