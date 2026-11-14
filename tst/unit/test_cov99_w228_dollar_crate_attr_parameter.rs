//@ crate-type: lib
//@ compile-fail: attribute is not allowed in this position:

macro_rules! attributed_parameter {
    () => {
        pub fn attributed(#[$crate::inline] _: ()) {}
    };
}

attributed_parameter!();
