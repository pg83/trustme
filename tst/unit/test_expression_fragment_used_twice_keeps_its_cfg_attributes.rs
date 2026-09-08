//@ run-pass
// bitflags 2.9 `bitflags! { .. const FOO = { #[cfg(target_os = "linux")] { 1 } #[cfg(not(..))] { 2 } }; }`
// (its `nested_value` test): the `$value:expr` fragment is emitted twice by one
// expansion, and the clone made for a later use lost the attributes of the
// statements inside, so both blocks stayed and the value was the last one.
macro_rules! flags {
    ($name:ident : $t:ty { $(const $f:ident = $value:expr;)* }) => {
        pub struct $name($t);
        pub struct Internal($t);
        impl Internal {
            $(pub const $f: Self = Internal($value);)*
        }
        impl $name {
            $(pub const $f: Self = $name($value);)*
            pub fn bits(&self) -> $t { self.0 }
        }
    };
}
flags! {
    Flags: u32 {
        const FOO = {
            #[cfg(target_os = "linux")] { 1 }
            #[cfg(not(target_os = "linux"))] { 2 }
        };
    }
}
fn main() {
    assert_eq!(1, Flags::FOO.bits());
    assert_eq!(1, Internal::FOO.0);
}
