// Extracted from src/names/name-resolution.md:99
#![allow(unused)]
fn main() {
    mod m {
        pub const C: () = ();
        pub enum E { V }
        pub type A = E;
        impl E {
            pub const C: () = ();
        }
    }
    // Invalid type-relative imports that can't resolve at expansion-time:
    use m::A::V; // ERROR: Unresolved import `m::A::V`.
    use m::E::C; // ERROR: Unresolved import `m::E::C`.
}
