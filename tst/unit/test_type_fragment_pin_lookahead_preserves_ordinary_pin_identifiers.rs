//@ extra-flags: -O
#![allow(non_camel_case_types)]

struct pin(u16);
static VALUE: pin = pin(321);

macro_rules! typed { ($ty:ty, $value:expr) => {{ let result: $ty = $value; result }}; }

fn main() {
    assert_eq!(typed!(&pin, &VALUE).0, 321);
    assert_eq!(typed!(&'static pin, &VALUE).0, 321);
    let reference = &VALUE;
    assert_eq!(typed!(&&pin, &reference).0, 321);
    let mut local = pin(7);
    typed!(&mut pin, &mut local).0 += 5;
    assert_eq!(local.0, 12);
}
