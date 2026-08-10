// Extracted from src/names/name-resolution.md:116
#![allow(unused)]
fn main() {
    pub mod m1 {
        pub mod ambig {
            pub const C: u8 = 1;
        }
    }
    
    pub mod m2 {
        pub mod ambig {
            pub const C: u8 = 2;
        }
    }
    
    // This introduces the name `ambig` in the outer scope.
    use m1::ambig;
    const _: () = {
        // This shadows `ambig` in the inner scope.
        use m2::ambig;
        // The inner candidate is selected here
        // as the resolution of `ambig`.
        use ambig::C;
        assert!(C == 2);
    };
}
