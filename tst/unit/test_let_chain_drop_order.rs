//@ edition: 2024
// A binding made by a `let` operand of an `&&` chain lives until the end of the
// body, whether or not another operand follows it. The operand that fails drops
// what the operands before it bound, on that branch alone.
use std::cell::RefCell;

struct Noisy<'a> {
    log: &'a RefCell<Vec<String>>,
    name: &'static str,
}

impl Drop for Noisy<'_> {
    fn drop(&mut self) {
        self.log.borrow_mut().push(format!("drop {}", self.name));
    }
}

struct Source<'a>(&'a RefCell<Vec<String>>);

impl<'a> Source<'a> {
    fn some(&self, name: &'static str) -> Option<Noisy<'a>> {
        self.0.borrow_mut().push(format!("make {}", name));
        Some(Noisy { log: self.0, name })
    }

    fn none(&self, name: &'static str) -> Option<Noisy<'a>> {
        self.0.borrow_mut().push(format!("make {}", name));
        None
    }

    fn mark(&self, name: &'static str) {
        self.0.borrow_mut().push(format!("mark {}", name));
    }
}

fn taken(log: &RefCell<Vec<String>>) {
    let source = Source(log);
    if let Some(_first) = source.some("first") && source.some("second").is_some() {
        source.mark("body");
    }
}

fn not_taken(log: &RefCell<Vec<String>>) {
    let source = Source(log);
    if let Some(_first) = source.some("first") && source.none("second").is_some() {
        source.mark("body");
    } else {
        source.mark("else");
    }
}

fn main() {
    let log = RefCell::new(Vec::new());
    taken(&log);
    assert_eq!(
        log.borrow().join(", "),
        "make first, make second, drop second, mark body, drop first"
    );

    let log = RefCell::new(Vec::new());
    not_taken(&log);
    assert_eq!(log.borrow().join(", "), "make first, make second, drop first, mark else");
}
