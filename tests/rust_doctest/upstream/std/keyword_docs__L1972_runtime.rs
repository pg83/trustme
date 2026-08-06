// Extracted from library/std/src/keyword_docs.rs:1972
#![allow(unused)]
#![allow(dead_code)]
fn main() {
    static mut FOO: &str = "hello";
    
    unsafe fn unsafe_fn() {}
    
    unsafe extern "C" {
        fn unsafe_extern_fn();
        static BAR: *mut u32;
    }
    
    trait SafeTraitWithUnsafeMethod {
        unsafe fn unsafe_method(&self);
    }
    
    struct S;
    
    impl S {
        unsafe fn unsafe_method_on_struct() {}
    }
}
