#![feature(decl_macro)]

pub fn captured() -> u16 { 0 }
pub macro invoke($value:expr) {{
    let captured = $value;
    { fn captured() -> u16 { 359 } captured() }
}}
