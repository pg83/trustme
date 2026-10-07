//@ proc-macro-aux-build: dollar_crate_probe.rs
// tor-cell's `restricted_msg!` (arti) captures message types as `$p:path` and
// writes them back into a derive-deftly input. Upstream hands a proc macro a
// captured `path` as the path's own tokens in an invisible group; we stopped
// at "TODO: visitToken - TOK_INTERPOLATED_PATH".
mod units {
    pub struct Meters(pub u32);
}

macro_rules! typed_const {
    ($name:ident: $p:path = $value:expr) => {
        dollar_crate_probe::echo! { const $name: $p = $value; }
    };
}

typed_const! { LIMIT: core::primitive::u32 = 7 }
typed_const! { WIDTH: units::Meters = units::Meters(3) }

fn main() {
    assert_eq!(LIMIT, 7);
    assert_eq!(WIDTH.0, 3);
}
