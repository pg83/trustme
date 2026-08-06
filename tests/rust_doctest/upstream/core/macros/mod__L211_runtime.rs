// Extracted from library/core/src/macros/mod.rs:211
#![allow(unused)]
#![feature(cfg_select)]
fn main() {
    
    cfg_select! {
        unix => {
            fn foo() { /* unix specific functionality */ }
        }
        target_pointer_width = "32" => {
            fn foo() { /* non-unix, 32-bit functionality */ }
        }
        _ => {
            fn foo() { /* fallback implementation */ }
        }
    }
}
