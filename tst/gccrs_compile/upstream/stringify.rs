
#![feature(rustc_attrs)]

fn main() {
    let _a = stringify!(sample text with parenthesis () and things! This will become a "string".);
}
