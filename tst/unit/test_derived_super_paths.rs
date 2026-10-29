//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
pub struct Number(pub u16);
mod outer {
    pub mod inner {
        #[derive(proc_macro_item_passthrough::HelperForward)]
        struct Wrapped(super::super::Number);
        pub fn check() {
            assert!({
                let value: super::super::Number = super::super::Number(17);
                Wrapped(value).0.0 == 17
            });
            assert_eq!(Wrapped::helper_forward_seen(), 1);
        }
    }
}
fn main() { outer::inner::check(); }
