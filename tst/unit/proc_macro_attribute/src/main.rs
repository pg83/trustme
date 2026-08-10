use tracing_attributes::instrument;

#[instrument]
fn replaced_function() {}

fn main() {
    assert_eq!(instrument_was_invoked(), 42);
}
