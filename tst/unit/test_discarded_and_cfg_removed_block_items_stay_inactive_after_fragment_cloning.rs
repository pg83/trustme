//@ compile-variant: -O
macro_rules! discard { ($body:expr) => { 7u8 }; }
macro_rules! pair { ($body:expr) => {{ let a = $body; let b = $body; (a, b) }}; }
fn main() {
    assert_eq!(discard!({ struct Unused(Missing); let _: Missing = loop {}; }), 7);
    let values = pair!({
        #[cfg(any())]
        struct Hidden(Missing);
        const VALUE: u8 = 11;
        VALUE
    });
    assert_eq!(values, (11, 11));
}
