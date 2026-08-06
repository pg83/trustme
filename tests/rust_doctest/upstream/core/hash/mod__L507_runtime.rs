// Extracted from library/core/src/hash/mod.rs:507
#![allow(unused)]
#![feature(hasher_prefixfree_extras)]
fn main() {
    struct Foo;
    impl std::hash::Hasher for Foo {
    fn finish(&self) -> u64 { unimplemented!() }
    fn write(&mut self, _bytes: &[u8]) { unimplemented!() }
    fn write_str(&mut self, s: &str) {
        self.write_length_prefix(s.len());
        self.write(s.as_bytes());
    }
    }
}
