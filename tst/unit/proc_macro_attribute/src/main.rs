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

fn main() {
    assert_eq!(instrument_was_invoked(), 42);
    suffixed_literal_roundtrip();
}
