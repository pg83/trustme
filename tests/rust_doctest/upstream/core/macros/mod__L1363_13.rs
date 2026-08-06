// Extracted from library/core/src/macros/mod.rs:1363
#![allow(unused)]
fn main() {
    mod test {
        pub fn foo() {
            assert!(module_path!().ends_with("test"));
        }
    }
    
    test::foo();
}
