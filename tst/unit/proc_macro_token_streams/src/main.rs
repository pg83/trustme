// The checks run while the macro expands - the only place the `proc_macro` API is
// available, as upstream's bridge has it; a failed check is a compile error here.
token_stream_checks::check_token_streams!();

fn main() {}
