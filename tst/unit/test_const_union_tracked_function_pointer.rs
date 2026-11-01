union Callback { call: fn() -> &'static str, address: usize }
#[track_caller]
fn caller_file() -> &'static str { std::panic::Location::caller().file() }
const VALUE: Callback = Callback { call: caller_file };

fn main() {
    let value = std::hint::black_box(VALUE);
    unsafe { assert_eq!((value.call)(), file!()); }
}
