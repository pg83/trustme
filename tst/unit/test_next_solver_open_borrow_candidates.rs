//@ check-pass
//@ compile-flags: -Znext-solver

use std::fmt::{Debug, Display};
use std::net::Ipv4Addr;
use std::cmp::Ordering;

#[derive(PartialEq, PartialOrd)]
struct LocalV4;

#[derive(Debug)]
struct DebugFields {
    size: usize,
    align: usize,
}

struct LocalLocation;

struct LocalPanicInfo<'a> {
    location: &'a LocalLocation,
}

impl LocalPanicInfo<'_> {
    fn location(&self) -> Option<&LocalLocation> {
        Some(&self.location)
    }
}

enum LocalIp {
    V4(LocalV4),
    V6,
}

#[derive(PartialEq, PartialOrd)]
enum DerivedIp {
    V4(LocalV4),
    V6(u8),
}

impl PartialEq<LocalIp> for LocalV4 {
    fn eq(&self, other: &LocalIp) -> bool {
        matches!(other, LocalIp::V4(_))
    }
}

impl PartialOrd<LocalIp> for LocalV4 {
    fn partial_cmp(&self, other: &LocalIp) -> Option<Ordering> {
        match other {
            LocalIp::V4(v4) => self.partial_cmp(v4),
            LocalIp::V6 => Some(Ordering::Less),
        }
    }
}

fn accept_display<T: Display>(_: &T) {}

fn accept_borrowed_str(value: &str) {
    accept_display(&value);
}

fn map_borrowed_str<'a>(value: &&'a str) -> &'a str {
    (|&text| text)(value)
}

fn borrowed_integer_literal(values: &[u8], index: usize) -> u8 {
    *values.get(index).unwrap_or(&0)
}

fn clone_option<T: Clone>(destination: &mut Option<T>, source: &Option<T>) {
    match (destination, source) {
        (Some(to), Some(from)) => to.clone_from(from),
        (to, from) => *to = from.clone(),
    }
}

fn compare_borrowed_ipv4(left: &Ipv4Addr, right: &Ipv4Addr) {
    let _ = left.partial_cmp(right);
}

fn zip_debug_fields(names: &[&str], values: &[&dyn Debug]) {
    assert_eq!(names.len(), values.len());
    for (name, value) in std::iter::zip(names, values) {
        let _: &str = name;
        let _: &&dyn Debug = value;
    }
}

fn main() {
    accept_borrowed_str("value");
    let value = "value";
    assert_eq!(map_borrowed_str(&value), value);
    assert_eq!(borrowed_integer_literal(&[], 0), 0);
    clone_option(&mut Some(1), &Some(2));
    compare_borrowed_ipv4(&Ipv4Addr::LOCALHOST, &Ipv4Addr::UNSPECIFIED);
    let _ = DerivedIp::V4(LocalV4) < DerivedIp::V6(0);
    zip_debug_fields(&[], &[]);
    let _ = format!("{:?}", DebugFields { size: 1, align: 1 });
    let _ = LocalPanicInfo { location: &LocalLocation }.location();
}
