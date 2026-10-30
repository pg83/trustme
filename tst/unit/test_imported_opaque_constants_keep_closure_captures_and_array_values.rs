//@ aux-build: foreign_constant_values.rs
use foreign_constant_values::{OPERATION,VALUES};
#[inline(never)]
fn invoke(value:u16) -> u16 { OPERATION(value) }
fn main() {
 assert_eq!(invoke(7),18);
 let operation=OPERATION;
 assert_eq!(operation(13),24);
 let borrowed=&OPERATION;
 assert_eq!(borrowed(17),28);
 assert_eq!(VALUES.as_ref(),&[3,5,7]);
 let values=VALUES;
 let copy=values;
 assert_eq!(copy.as_ref(),&[3,5,7]);
}
