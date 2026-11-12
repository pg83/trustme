#![feature(decl_macro)]
#![no_std]

pub fn value() -> u16 { 61 }

pub macro answer() {{
    fn generated() -> u16 { value() }
    generated()
}}
