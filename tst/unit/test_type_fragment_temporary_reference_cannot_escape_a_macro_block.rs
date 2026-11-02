//@ compile-fail: temporary value dropped while borrowed
#![allow(non_camel_case_types)]

struct pin(u16);
static VALUE: pin = pin(321);

macro_rules! typed { ($ty:ty, $value:expr) => {{ let result: $ty = $value; result }}; }

fn main() {
    assert_eq!(typed!(&&pin, &&VALUE).0, 321);
}
