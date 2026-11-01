#![feature(decl_macro)]

pub static mut COUNTER: u16 = 7;
pub macro increment() {{ unsafe { COUNTER += 1; COUNTER } }}
