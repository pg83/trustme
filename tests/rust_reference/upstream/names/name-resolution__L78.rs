// Extracted from src/names/name-resolution.md:78
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
    
    // Valid imports resolved at expansion-time:
    use m::C; // OK.
    use m::E; // OK.
    use m::A; // OK.
    use m::E::V; // OK.
    
    // Valid expressions resolved during type-relative resolution:
    let _ = m::A::V; // OK.
    let _ = m::E::C; // OK.
}
