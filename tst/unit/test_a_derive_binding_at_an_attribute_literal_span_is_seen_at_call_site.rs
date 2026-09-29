//@ run-pass
//@ proc-macro-aux-build: default_from_path.rs
// schemars' `MyStruct2` has `#[serde(default = "ten_and_true")]`, and serde's
// Deserialize binds `let __default` at the span of the path parsed from that
// literal and reads `__default.boolean` at `Span::call_site()`. Both names
// are written in the crate the derive is used in and name one local, as in
// rustc; ours reported `Couldn't find variable name '__default'`. An
// attribute macro's call site is its attribute the same way.
extern crate default_from_path;

use default_from_path::{default_from_attr, DefaultFrom};

fn seven() -> Config {
    Config { level: 7 }
}

#[derive(DefaultFrom)]
#[default_from = "seven"]
struct Config {
    level: u32,
}

fn eight() -> Settings {
    Settings { depth: 8 }
}

#[default_from_attr("eight")]
struct Settings {
    depth: u32,
}

fn main() {
    assert_eq!(Config::made(), 7);
    assert_eq!(Settings::made(), 8);
}
