//@ crate-type: lib
#![feature(extern_types)]
unsafe extern "C" { pub type Foreign; }
pub fn shared_address(value: &Foreign) -> *const Foreign {
    match *value { ref place => place as *const Foreign }
}
pub fn unique_address(value: &mut Foreign) -> *mut Foreign {
    match *value { ref mut place => place as *mut Foreign }
}
