//@ reject-flags: --cfg hidden => Ancestor || private
pub mod outer {
    pub mod inner {
        pub(in crate::outer) struct Ancestor(pub u16);
    }
    pub fn read() -> u16 { inner::Ancestor(293).0 }
}
#[cfg(hidden)]
fn main() { let _ = outer::inner::Ancestor(1); }
#[cfg(not(hidden))]
fn main() { assert_eq!(outer::read(), 293); }
