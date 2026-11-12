struct Pair {
    left: u16,
    right: u16,
}

enum Choice {
    Pair(Pair),
    Single(u16),
}

union Bits {
    word: u16,
    bytes: [u8; 2],
}

#[inline(never)]
fn project(value: Choice, input: &[u16], index: usize, pointer: *const u16) -> u16 {
    let selected = match value {
        Choice::Pair(Pair { left, right }) => left + right,
        Choice::Single(value) => value,
    };
    selected + input[index] + unsafe { *pointer }
}

fn main() {
    let cells = [1133u16, 7u16];
    assert_eq!(
        project(Choice::Pair(Pair { left: 2, right: 3 }), &cells, 1, &cells[0]),
        1145
    );
    assert_eq!(project(Choice::Single(11), &cells, 0, &cells[1]), 1151);

    let (head, [left, right]) = (cells[0], [17u16, 19u16]);
    assert_eq!(head + left + right, 1169);

    let bits = Bits { word: 0x1234 };
    assert_eq!(unsafe { bits.word }, 0x1234);

    let boxed = Box::new(23u16);
    assert_eq!(*boxed, 23);
}
