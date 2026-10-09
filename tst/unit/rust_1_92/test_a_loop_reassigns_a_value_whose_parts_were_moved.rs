// A loop that reassigns a variable after moving parts of it out. rustc's
// drop elaboration keeps a flag per move path (MaybeInitializedPlaces), so
// the drop on reassignment skips the moved payload or field. Our loop head
// gave such a variable one whole flag, set again at the back edge, and the
// next reassignment dropped `Err(err)`'s moved payload a second time
// (fs_extra). Heads are now shaped by fields and variants; a variable valid
// on entry gets one only when the body moves out of it, so a guard whose
// field is just reassigned keeps no flag.
#[derive(Clone, Copy, PartialEq)]
enum Kind {
    Exists,
    Other,
}

struct Error {
    message: String,
    kind: Kind,
}

fn attempt(n: u32) -> Result<u64, Error> {
    if n < 2 {
        Err(Error { message: format!("exists {n}"), kind: Kind::Exists })
    } else {
        Ok(5)
    }
}

fn declared_before_the_loop() -> (u64, Vec<usize>) {
    let mut result_copy: Result<u64, Error>;
    let mut work = true;
    let mut n = 0;
    let mut total = 0;
    let mut seen = Vec::new();
    while work {
        result_copy = attempt(n);
        match result_copy {
            Ok(val) => {
                total += val;
                work = false;
            }
            Err(err) => match err.kind {
                Kind::Exists => {
                    seen.push(err.message.len());
                    n += 1;
                }
                Kind::Other => break,
            },
        }
    }
    (total, seen)
}

fn valid_before_the_loop() -> Vec<String> {
    let mut current = attempt(5);
    let mut n = 0;
    let mut messages = Vec::new();
    loop {
        current = attempt(n);
        match current {
            Ok(_) => break,
            Err(err) => {
                messages.push(err.message);
                n += 1;
            }
        }
    }
    messages
}

struct Guard<'a> {
    log: &'a std::cell::RefCell<Vec<usize>>,
    at: usize,
}

impl Drop for Guard<'_> {
    fn drop(&mut self) {
        self.log.borrow_mut().push(self.at);
    }
}

fn guard_field_assigned_in_the_loop() -> Vec<usize> {
    let log = std::cell::RefCell::new(Vec::new());
    {
        let mut guard = Guard { log: &log, at: 0 };
        loop {
            guard.at = guard.at + 1;
            if guard.at == 3 {
                break;
            }
        }
    }
    log.into_inner()
}

struct Pair {
    first: String,
    second: String,
}

fn field_moved_and_refilled() -> Vec<String> {
    let mut pair = Pair { first: String::from("a0"), second: String::from("b") };
    let mut taken = Vec::new();
    for i in 1..3 {
        taken.push(pair.first);
        pair.first = format!("a{i}");
    }
    taken.push(pair.second);
    taken.push(pair.first);
    taken
}

fn field_moved_after_assignment() -> Vec<String> {
    let mut pair: Pair;
    let mut taken = Vec::new();
    let mut i = 0;
    loop {
        pair = Pair { first: format!("a{i}"), second: format!("b{i}") };
        taken.push(pair.first);
        i += 1;
        if i == 2 {
            break;
        }
    }
    taken.push(pair.second);
    taken
}

fn main() {
    assert_eq!(declared_before_the_loop(), (5, vec![8, 8]));
    assert_eq!(valid_before_the_loop(), ["exists 0", "exists 1"]);
    assert_eq!(guard_field_assigned_in_the_loop(), [3]);
    assert_eq!(field_moved_and_refilled(), ["a0", "a1", "b", "a2"]);
    assert_eq!(field_moved_after_assignment(), ["a0", "a1", "b1"]);
}
