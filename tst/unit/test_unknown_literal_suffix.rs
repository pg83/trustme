// A suffix that names no type still lexes as one literal token, so code
// carrying it parses. Only lowering rejects it, and this code is never lowered.
#![allow(unused)]

fn main() {
    #[cfg(false)]
    {
        0invalidSuffix;
        123AFB43;
        0b1111_f32;
        2.0f80;
        2e5e6;
        1.3e10u64;
    }
}
