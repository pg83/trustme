trait Shape {
    type Word: PartialEq + Eq;
}

impl Shape for () {
    type Word = u16;
}

#[derive(PartialEq, Eq)]
struct Cell<T: Shape> {
    value: T::Word,
}

#[derive(PartialEq, Eq)]
enum Packet<T: Shape> {
    Empty,
    Full(Cell<T>),
    Code(T::Word),
}

const CELL: Cell<()> = Cell { value: 23 };
const PACKET: Packet<()> = Packet::Full(Cell { value: 41 });
const PAIR: (<() as Shape>::Word, Cell<()>) = (17, Cell { value: 31 });

fn cell(value: Cell<()>) -> bool {
    matches!(value, CELL)
}

fn packet(value: Packet<()>) -> bool {
    matches!(value, PACKET)
}

fn pair(value: (u16, Cell<()>)) -> bool {
    matches!(value, PAIR)
}

fn main() {
    assert!(cell(Cell { value: 23 }));
    assert!(!cell(Cell { value: 24 }));
    assert!(packet(Packet::Full(Cell { value: 41 })));
    assert!(!packet(Packet::Full(Cell { value: 42 })));
    assert!(!packet(Packet::Empty));
    assert!(!packet(Packet::Code(41)));
    assert!(pair((17, Cell { value: 31 })));
    assert!(!pair((18, Cell { value: 31 })));
}
