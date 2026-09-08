// `proc_macro::Span::call_site()` outside a procedural macro panics (upstream's bridge is
// not set up there); proc-macro2 0.4 probes that with `catch_unwind` to choose fallback
// spans, and a quiet answer gave it compiler spans whose `start()` is line 0
// (version-sync's `html_root_url` check in clap 2.33's `version-numbers` test).
extern crate proc_macro;

fn main() {
    assert!(!proc_macro::is_available());
    std::panic::set_hook(Box::new(|_| {}));
    let probe = std::panic::catch_unwind(|| proc_macro::Span::call_site());
    assert!(probe.is_err());
}
