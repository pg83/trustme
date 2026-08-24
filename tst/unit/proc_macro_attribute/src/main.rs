use tracing_attributes::{identity, instrument};

macro_rules! discard_one_token {
    ($token:tt) => {};
}

#[instrument]
fn replaced_function() {}

#[identity]
fn suffixed_literal_roundtrip() {
    discard_one_token!("string"suffix);
}

#[identity]
const ATTRIBUTE_CONST: u32 = 17;

#[identity]
static ATTRIBUTE_STATIC: u32 = 25;

fn main() {
    assert_eq!(instrument_was_invoked(), 42);
    assert_eq!(unicode_char_from_token_stream(), '\u{2764}');
    assert_eq!(ATTRIBUTE_CONST + ATTRIBUTE_STATIC, 42);
    suffixed_literal_roundtrip();
}
