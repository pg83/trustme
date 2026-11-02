//@ reject-flags: --cfg invalid_number => No macro arm matched || expected
//@ reject-flags: --cfg invalid_let => No macro arm matched || expected
macro_rules! item { ($value:item) => { $value }; }

#[cfg(invalid_number)]
item!(17);

#[cfg(invalid_let)]
item!(let value = 19;);

item!(const VALUE: u16 = 23;);

fn main() { assert_eq!(VALUE, 23); }
