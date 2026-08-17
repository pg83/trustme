// The `unsafe_code` lint reports the user's own `unsafe`, not what a macro from
// another crate expanded to: `thread_local!` is unsafe inside.
#![forbid(unsafe_code)]

thread_local!(static COUNT: u8 = 1);

fn main() {
    COUNT.with(|value| assert_eq!(*value, 1));
}
