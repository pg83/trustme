// asn1-rs 0.7.2's impl_int: `trace_generic(name, msg, |any| { .. decode(&any) .. }, any)`
// with `F: Fn(I) -> O`. rustc's check_argument_types checks the arguments that
// are not closures first and the closures after them, so `I` is `&Any` from
// the last argument before the closure's body coerces `&any` to `&Any`.
pub struct Any {
    bytes: Vec<u8>,
}

fn trace_generic<F, I, O>(f: F, input: I) -> O
where
    F: Fn(I) -> O,
{
    f(input)
}

fn first_byte(any: &Any) -> u8 {
    any.bytes[0]
}

fn convert(any: &Any) -> u8 {
    trace_generic(|any| first_byte(&any), any)
}

fn main() {
    assert_eq!(convert(&Any { bytes: vec![7] }), 7);
}
