// `response.metadata.id = id` in a loop that also moves and reassigns
// `response` (arti's DNS proxy). The loop head gives `response` a flag per
// part; `metadata` has no drop glue, and the field assignment split it, so
// the back edge found it "partially moved" with no part to take a flag
// from. rustc's drop elaboration keeps no flags for paths that need no drop,
// and a type with its own `Drop` has no interior paths: a field assigned in
// it leaves it as initialised as it was.
#[derive(Clone, Copy, Debug, PartialEq)]
struct Header {
    id: u16,
    flags: u8,
}

#[derive(Debug)]
struct Message {
    metadata: Header,
    answers: Vec<String>,
}

fn consume(m: Message) -> usize {
    m.answers.len()
}

fn replies(ids: &[u16]) -> Vec<(u16, usize)> {
    let mut response = Message { metadata: Header { id: 0, flags: 1 }, answers: vec![String::from("a")] };
    let mut out = Vec::new();
    for &id in ids {
        response.metadata.id = id;
        if id == 0 {
            out.push((id, consume(response)));
            response = Message { metadata: Header { id: 9, flags: 2 }, answers: Vec::new() };
            continue;
        }
        out.push((response.metadata.id, response.answers.len()));
    }
    out
}

struct Guard<'a> {
    log: &'a std::cell::RefCell<Vec<u32>>,
    at: u32,
}

impl Drop for Guard<'_> {
    fn drop(&mut self) {
        self.log.borrow_mut().push(self.at);
    }
}

fn guard_moved_and_field_assigned() -> Vec<u32> {
    let log = std::cell::RefCell::new(Vec::new());
    {
        let mut guard = Guard { log: &log, at: 0 };
        for i in 0..3 {
            guard.at = i;
            if i == 1 {
                drop(guard);
                guard = Guard { log: &log, at: 10 };
            }
        }
    }
    log.into_inner()
}

fn main() {
    assert_eq!(guard_moved_and_field_assigned(), [1, 2]);
    assert_eq!(replies(&[3, 0, 4]), [(3, 1), (0, 1), (4, 0)]);
}
