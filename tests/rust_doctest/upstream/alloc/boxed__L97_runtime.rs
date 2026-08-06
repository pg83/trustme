// Extracted from library/alloc/src/boxed.rs:97
#![allow(unused)]
extern crate alloc;
fn main() {
    #[repr(C)]
    pub struct Foo;
    
    #[unsafe(no_mangle)]
    pub extern "C" fn foo_new() -> Box<Foo> {
        Box::new(Foo)
    }
    
    #[unsafe(no_mangle)]
    pub extern "C" fn foo_delete(_: Option<Box<Foo>>) {}
}
