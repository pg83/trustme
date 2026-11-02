//@ compile-variant: -O
#[inline(never)]
fn mutable(first: bool, value: &mut u16, raw: *mut u16) -> *mut u16 {
    let result = if first { value } else { raw };
    result
}
#[inline(never)]
fn shared(first: bool, value: &u16, raw: *const u16) -> *const u16 {
    let result = if first { raw } else { value };
    result
}
#[inline(never)]
fn raw_forms(first: bool, left: *mut u16, right: *const u16) -> *const u16 {
    let result = if first { left } else { right };
    result
}
#[inline(never)]
fn borrowed<'a>(first: bool, left: &'a mut u16, right: &'a u16) -> &'a u16 {
    let result = if first { left } else { right };
    result
}
fn main() {
    let mut left = std::hint::black_box(17u16);
    let mut right = std::hint::black_box(23u16);
    let raw = &mut right as *mut u16;
    for first in [true, false] {
        unsafe { assert_eq!(*mutable(first, &mut left, raw), if first { 17 } else { 23 }); }
        unsafe { assert_eq!(*shared(first, &left, raw), if first { 23 } else { 17 }); }
        unsafe { assert_eq!(*raw_forms(first, &mut left, raw), if first { 17 } else { 23 }); }
        assert_eq!(*borrowed(first, &mut left, &right), if first { 17 } else { 23 });
    }
}
