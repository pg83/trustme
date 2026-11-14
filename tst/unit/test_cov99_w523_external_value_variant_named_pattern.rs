//@ aux-build: cov99_w523_external_value_variant.rs

use cov99_w523_external_value_variant::State;
use cov99_w523_external_value_variant::State::Ready;

fn classify(state: State) -> u8 {
    match state {
        Ready { .. } => 17,
        _ => 0,
    }
}

fn main() {
    assert_eq!(classify(State::Ready), 17);
    assert_eq!(classify(State::Waiting), 0);
}
