//@ aux-build: legacy_visibility_tokens.rs
//@ compile-variant: -O

mod outer {
    mod inner {
        legacy_visibility_tokens::relative!(relative);
        legacy_visibility_tokens::absolute!(absolute);
    }
    pub fn value() -> u16 { inner::relative() + inner::absolute() }
}

fn main() { assert_eq!(outer::value(), 240); }
