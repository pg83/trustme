//@ run-pass
//@ edition: 2015
//@ aux-build: cfg_if_2018.rs
//@ aux-build: libc_stub.rs
// net2 0.2 `ext.rs`: `use libc::TCP_KEEPIDLE as KEEPALIVE_OPTION;` inside `cfg_if!`
// of the 2018-edition cfg-if crate.  The `use` is written in this 2015 crate, so
// its path names the crate root (`::libc`, the `extern crate` below), whatever the
// edition of the macro it passes through: a token keeps the edition of the crate
// that wrote it, as upstream's spans do.  Stamping the whole nested invocation
// with the macro's 2018 edition read `libc` relative to `ext` and found nothing.
#[macro_use]
extern crate cfg_if_2018;
extern crate libc_stub as libc;

mod ext {
    cfg_if! {
        if #[cfg(any(target_os = "macos", target_os = "ios"))] {
            use libc::c_long as Opt;
        } else if #[cfg(unix)] {
            use libc::c_int as Opt;
        } else {
            use libc::c_short as Opt;
        }
    }

    pub fn option() -> Opt {
        7
    }
}

fn main() {
    assert_eq!(ext::option(), 7);
}
