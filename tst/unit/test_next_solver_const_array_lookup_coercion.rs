//@ check-pass

struct Header(usize);

impl Header {
    const fn start_index(&self) -> usize {
        self.0
    }
}

static HEADERS: [Header; 1] = [Header(0)];
static OFFSETS: [u8; 1] = [0];

fn lookup() {
    const {
        let mut index = 0;
        while index < HEADERS.len() {
            assert!(HEADERS[index].start_index() < OFFSETS.len());
            index += 1;
        }
    }
}

fn main() {
    lookup();
}
