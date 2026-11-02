//@ reject-flags: --cfg missing_name => No macro arm matched || expected
//@ reject-flags: --cfg missing_body => No macro arm matched || expected
#![feature(decl_macro)]

macro_rules! accept { ($item:item) => { $item }; }

#[cfg(missing_name)]
accept!(macro 7 {});

#[cfg(missing_body)]
accept!(macro incomplete() 17);

accept!(macro complete() { 53u16 });

fn main() { assert_eq!(complete!(), 53); }
