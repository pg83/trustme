//@ compile-fail: macro_rules! macros can't take an ident

macro_rules! emit {
    () => { fn main() {} };
}

emit! unexpected_name {}
