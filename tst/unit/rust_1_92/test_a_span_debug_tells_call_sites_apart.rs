//@ proc-macro-aux-build: call_site_debug.rs
// const-random hashes `format!("{:?}", Span::call_site())` into each value it
// generates. rustc's `Debug` for a proc-macro `Span` goes to the server and
// prints where the span is (`#ctxt bytes(lo..hi)`, or with `-Zspan-debug`
// `file:line:col: line:col (#ctxt)`), so two invocations print differently.
// Ours derived `Debug` from the span's handle, and the call site is always
// handle 1: every `const_random!` in a crate yielded the same number.
use call_site_debug::call_site_debug;

fn main() {
    let first = call_site_debug!();
    let second = call_site_debug!();
    assert_ne!(first, second);
}
