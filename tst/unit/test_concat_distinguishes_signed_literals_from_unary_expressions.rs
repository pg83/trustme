//@ reject-flags: --cfg logical_not => Unexpected expression type in concat! argument || error
//@ reject-flags: --cfg negative_bool => Unexpected expression type in concat! argument || error

#[cfg(logical_not)]
const INVALID: &str = concat!(!true);
#[cfg(negative_bool)]
const INVALID: &str = concat!(-true);

fn main() {
    assert_eq!(concat!(-17,), "-17");
    assert_eq!(concat!(), "");
    assert_eq!(concat!(1.25, "/", -2.5), "1.25/-2.5");
}
