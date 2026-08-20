//@ compile-fail: local ambiguity
//@ crate-type: lib

macro_rules! ambiguity {
    ($($item:ident)* $last:ident) => {};
}

ambiguity!(value);
