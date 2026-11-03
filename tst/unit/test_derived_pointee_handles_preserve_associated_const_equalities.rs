//@ compile-flags: -Zcrate-attr=feature(cfg_version) '-Zcrate-attr=cfg_attr(version("1.96"),feature(min_generic_const_args))' '-Zcrate-attr=cfg_attr(not(version("1.96")),feature(associated_const_equality))'
#![feature(derive_coerce_pointee)]
use std::marker::CoercePointee;

trait Measured { const WIDTH: usize; }
impl Measured for [u8; 4] { const WIDTH: usize = 4; }
impl Measured for [u8] { const WIDTH: usize = 4; }

#[derive(CoercePointee)]
#[repr(transparent)]
struct Handle<'a, T: ?Sized + Measured<WIDTH = 4>>(&'a T);

fn main() {
    let bytes = [3, 5, 7, 11];
    let erased: Handle<'_, [u8]> = Handle(&bytes);
    assert_eq!(erased.0, &[3, 5, 7, 11]);
}
