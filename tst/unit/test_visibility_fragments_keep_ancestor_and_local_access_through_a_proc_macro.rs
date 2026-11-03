//@ proc-macro-aux-build: type_phase_identity.rs
//@ reject-flags: --cfg hidden => Ancestor || private
macro_rules! expose {
    ($visibility:vis, $name:ident) => {
        type_phase_identity::emit! { $visibility struct $name(pub u16); }
    };
}
pub mod outer {
    pub mod middle {
        pub mod inner {
            expose!(pub(in crate::outer), Ancestor);
            expose!(pub(self), Local);
            expose!(pub(super), Parent);
            pub(in crate::outer) fn local() -> u16 { Local(271).0 }
        }
        pub(in crate::outer) fn sum() -> u16 {
            inner::Ancestor(277).0 + inner::Parent(281).0 + inner::local()
        }
    }
    pub fn read() -> u16 { middle::sum() + middle::inner::Ancestor(283).0 }
}
#[cfg(hidden)]
fn main() { let _ = outer::middle::inner::Ancestor(1); }
#[cfg(not(hidden))]
fn main() { assert_eq!(outer::read(), 1112); }
