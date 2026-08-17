// `cfg(ub_checks)` says whether the library's UB checks are compiled in. It
// follows debug assertions unless `-Zub-checks` says otherwise, and was never
// set at all.
//
// Same shape as the upstream test precondition-checks/cfg-ub-checks-yes.rs.
#![feature(cfg_ub_checks)]

#[cfg(ub_checks)]
fn compiledWith() -> bool {
    true
}

#[cfg(not(ub_checks))]
fn compiledWith() -> bool {
    false
}

fn main() {
    // The unit tests are built with debug assertions, so the checks are in.
    assert!(cfg!(ub_checks));
    assert!(compiledWith());

    // And debug assertions are on, which is what it follows.
    assert!(cfg!(debug_assertions));
}
