#[cfg_attr(all(), path = "aux/parser_cfg_attr_value.rs", allow(dead_code))]
mod chosen;
fn main() { assert_eq!(chosen::value(), 37); }
