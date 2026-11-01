pub static DATA: u32 = 37;
pub const CONSTANT: u32 = 13;
pub fn value() -> u32 { 41 }
pub fn width<T>() -> usize { std::mem::size_of::<T>() }

#[inline(always)]
pub extern "C" fn only_in_asm<T>() -> usize { std::mem::size_of::<T>() + 29 }

#[export_name = "trustme_asm_symbol_named"]
pub extern "C" fn renamed() -> u32 { 43 }
