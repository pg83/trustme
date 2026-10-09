#![forbid(unsafe_code)]
// core 1.93 represents `fmt::Arguments` as a byte template and a slice of
// `fmt::rt::Argument`s; `fmt::rt::Placeholder` and `new_v1` are gone. rustc's
// lowering (rustc_ast_lowering/src/format.rs) encodes literal pieces with
// their length, each placeholder as a 0b11xxxxxx byte with optional flags,
// width, precision and argument index, deduplicates (argument, trait) pairs,
// and calls `Arguments::from_str` when there is no placeholder. The unsafe
// block it wraps around `Arguments::new` is compiler-generated: a crate that
// forbids unsafe code still formats.
use std::fmt::Write;

#[derive(Debug)]
struct Point {
    x: i32,
    y: i32,
}

fn check(got: &str, want: &str) {
    assert_eq!(got, want);
}

fn main() {
    let name = "world";
    let width = 9;
    let prec = 3;
    check(&format!("hello {name}\n"), "hello world\n");
    check(&format!("[{:>8}] [{:<6}] [{:^7}] [{:*^9}]", 42, "ab", 'c', "mid"), "[      42] [ab    ] [   c   ] [***mid***]");
    check(&format!("[{:+}] [{:08.3}] [{:#x}] [{:#010b}] [{:o}] [{:X}]", 5, -3.14159, 255, 5, 8, 3054), "[+5] [-003.142] [0xff] [0b00000101] [10] [BEE]");
    check(&format!("[{0:1$}] [{2:w$.p$}]", 7, width, 2.718281828, w = width, p = prec), "[        7] [    2.718]");
    check(&format!("[{:.*}] [{:>2$}]", 2, 1.23456, 6), "[1.23] [     6]");
    check(&format!("{0} {1} {0} {1:?} {0:x}", 10, "s"), "10 s 10 \"s\" a");
    check(&format!("{:?} {:#?}", Point { x: 1, y: -2 }, Some(3)), "Point { x: 1, y: -2 } Some(\n    3,\n)");
    check(&format!("{:x?} {:#X?}", vec![10, 11], [255u8]), "[a, b] [\n    0xFF,\n]");
    check(&format!("{:e} {:E} {:p}", 1234.5, 0.00012, std::ptr::null::<u8>()), "1.2345e3 1.2E-4 0x0");
    let long = "x".repeat(200);
    check(&format!("{}|{long}|{}", "a", "b"), &format!("a|{}|b", "x".repeat(200)));
    let huge = "y".repeat(70000);
    let s = format!("<{}{huge}>", 1);
    assert_eq!(s.len(), 70003);
    assert!(s.starts_with("<1yyy") && s.ends_with("yyy>"));
    let mut out = String::new();
    write!(out, "{}-{}", 1, 2).unwrap();
    check(&out, "1-2");
    assert_eq!(format_args!("plain").as_str(), Some("plain"));
    assert_eq!(format_args!("").as_str(), Some(""));
    assert_eq!(format!("{{}} {{{}}}", 1), "{} {1}");
    assert_eq!(std::fmt::format(format_args!("{}+{:?}", 1, "x")), "1+\"x\"");
}
